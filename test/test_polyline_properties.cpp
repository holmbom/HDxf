#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// A POLYLINE's VERTEX records and SEQEND follow the layer, linetype and color their polyline has been given, and have its linetype scale, lineweight and transparency in the files that have them; an INSERT's SEQEND has its
// layer, color, linetype scale and lineweight. The facit is AutoCAD's own: doc/facit/acad_<version>_dxf_polyline_owner*.dxf.
// ---------------------------------------------------------------------------

namespace {

using hdxf::Color;
using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::DxfVersion;

using Group = std::pair<int, std::string>;
using Record = std::vector<Group>;

struct VersionTag {
    DxfVersion version;
    const char* tag;
};

const VersionTag kFacitVersions[] = {
    {DxfVersion::R12, "r12"},     {DxfVersion::R2000, "2000"},  {DxfVersion::R2004, "2004"},
    {DxfVersion::R2007, "2007"},  {DxfVersion::R2010, "2010"},  {DxfVersion::R2013, "2013"},
    {DxfVersion::R2018, "2018"},
};

std::string trimmed(const std::string& s) {
    size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(' ') - a + 1);
}

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
        out.back().emplace_back(gc.code, trimmed(gc.value));
    }
    return out;
}

// The properties a child record has of its owner, as text; "" where the record has none. Numbers are normalized.
struct Props {
    std::string layer, linetype, color, scale, weight, transparency;

    bool operator==(const Props& o) const {
        return layer == o.layer && linetype == o.linetype && color == o.color && scale == o.scale && weight == o.weight &&
               transparency == o.transparency;
    }
    std::string text() const {
        return "8=" + layer + " 6=" + linetype + " 62=" + color + " 48=" + scale + " 370=" + weight + " 440=" + transparency;
    }
};

Props props_of(const Record& r) {
    Props p;
    auto number = [](const std::string& s) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.9g", std::strtod(s.c_str(), nullptr));
        return std::string(buf);
    };
    auto integer = [](const std::string& s) { return std::to_string(std::atoi(s.c_str())); };
    for (const Group& g : r) {
        if (g.first == 100 && !p.layer.empty()) break;      // the properties are in the head
        if (g.first == 8 && p.layer.empty()) p.layer = g.second;
        if (g.first == 6 && p.linetype.empty()) p.linetype = g.second;
        if (g.first == 62 && p.color.empty()) p.color = integer(g.second);
        if (g.first == 48 && p.scale.empty()) p.scale = number(g.second);
        if (g.first == 370 && p.weight.empty()) p.weight = integer(g.second);
        if (g.first == 440 && p.transparency.empty()) p.transparency = integer(g.second);
    }
    return p;
}

// POLYLINE, VERTEX, SEQEND and INSERT records of ENTITIES in file order (ATTRIB does not follow its owner)
std::vector<std::pair<std::string, Props>> chains_of(const std::vector<uint8_t>& data) {
    std::vector<std::pair<std::string, Props>> out;
    bool entities = false;
    for (const Record& r : records_of(data)) {
        const std::string type = r.empty() ? std::string() : r[0].second;
        if (type == "SECTION") {
            entities = r.size() > 1 && r[1].first == 2 && r[1].second == "ENTITIES";
            continue;
        }
        if (!entities) continue;
        if (type == "POLYLINE" || type == "VERTEX" || type == "SEQEND" || type == "INSERT") out.emplace_back(type, props_of(r));
    }
    return out;
}

std::vector<uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::filesystem::path facit_path(const std::string& version, const std::string& state, bool binary) {
    std::string name = "acad_" + version + "_dxf_polyline_owner";
    if (!state.empty()) name += "_" + state;
    if (binary) name += "_bin";
    return hdxf::path_from_utf8(HDXF_SOURCE_DIR) / "doc" / "facit" / hdxf::path_from_utf8(name + ".dxf");
}

Drawing load(const std::vector<uint8_t>& bytes) {
    Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(bytes.data(), bytes.size()), dwg.last_error());
    return dwg;
}

std::vector<uint8_t> save(Drawing& dwg, DxfFormat format = DxfFormat::Ascii) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), "save: " + dwg.last_error());
    return out;
}

// The states of the facit, each with what the one before had: color 3, then linetype DASHED, then layer LA, then lineweight
// 0.5 mm, linetype scale 2 and transparency 40 %
void apply_states(Drawing& dwg, DxfVersion version, size_t states) {
    for (auto& e : dwg.entities()) {
        if (e.type != hdxf::EntityType::Polyline && e.type != hdxf::EntityType::Insert) continue;
        if (states >= 1) e.props.color = Color::from_aci(3);
        if (states >= 2) e.props.linetype = "DASHED";
        if (states >= 3) e.props.layer = "LA";
        if (states >= 4 && version >= DxfVersion::R2000) {
            e.props.lineweight = 50;
            e.props.linetype_scale = 2.0;
            if (version >= DxfVersion::R2004) e.props.transparency = hdxf::Transparency::from_alpha(153);
        }
    }
}

// A polyline of three vertices and a drawing for it: layers LA and L2, linetypes DASHED and DOTTED
Drawing made_drawing(DxfVersion version) {
    Drawing dwg(version);
    dwg.add_layer("LA");
    dwg.add_layer("L2");
    dwg.add_linetype("DASHED", "Dashed", {0.5, -0.25});
    dwg.add_linetype("DOTTED", "Dotted", {0.0, -0.25});
    dwg.add_polyline({{0, 0}, {10, 0}, {10, 10}}, false);
    return dwg;
}

hdxf::Entity* polyline_of(Drawing& dwg) {
    for (auto& e : dwg.entities()) {
        if (e.type == hdxf::EntityType::Polyline) return &e;
    }
    return nullptr;
}

// A vertex or SEQEND layer as read is empty where it is the polyline's
std::string layer_of(const std::string& own, const hdxf::Entity& owner) { return own.empty() ? owner.props.layer : own; }

bool same_color(const Color& a, const Color& b) { return a.aci == b.aci && a.rgb == b.rgb && a.name == b.name; }

} // namespace

// AutoCAD's states: the owner changed in the API as it was changed in AutoCAD, the children are what AutoCAD's are
TEST(children_follow_owner) {
    size_t compared = 0;
    const char* const states[] = {"recolored", "relinetyped", "relayered", "reweighted"};
    for (const auto& v : kFacitVersions) {
        for (bool binary : {false, true}) {
            const std::filesystem::path base = facit_path(v.tag, "", binary);
            if (!std::filesystem::exists(base)) continue;
            for (size_t state = 1; state <= 4; ++state) {
                const std::filesystem::path next = facit_path(v.tag, states[state - 1], binary);
                if (!std::filesystem::exists(next)) continue;
                if (state == 4 && v.version == DxfVersion::R12) continue;      // R12 has no lineweight, scale or transparency
                const std::string what = std::string(v.tag) + (binary ? " binary " : " ascii ") + states[state - 1];
                Drawing dwg = load(read_bytes(base));
                apply_states(dwg, v.version, state);
                std::vector<uint8_t> out = save(dwg, binary ? DxfFormat::Binary : DxfFormat::Ascii);
                auto ours = chains_of(out);
                auto facit = chains_of(read_bytes(next));
                hdxf_test::assert_eq(static_cast<int>(facit.size()), static_cast<int>(ours.size()), what + ": the records");
                for (size_t i = 0; i < facit.size(); ++i) {
                    hdxf_test::assert_eq(facit[i].first, ours[i].first, what + ": record " + std::to_string(i));
                    hdxf_test::assert_true(facit[i].second == ours[i].second,
                                           what + ": " + facit[i].first + " " + std::to_string(i) + ": " + ours[i].second.text() +
                                               " but AutoCAD has " + facit[i].second.text());
                }
                ++compared;
            }
        }
    }
    if (compared == 0) hdxf_test::skip("no polyline_owner facit files");
}

// What a vertex or SEQEND had that was not the owner's as read stays: its own color, layer and linetype
TEST(children_keep_overrides) {
    for (DxfVersion version : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2018}) {
        Drawing dwg = made_drawing(version);
        hdxf::PolylineData& d = dwg.entities().back().as_polyline();
        d.vertices[1].color = Color::from_aci(1);
        d.vertices[1].layer = "L2";
        d.vertices[2].linetype = "DOTTED";
        d.seqend_color = Color::from_aci(2);
        Drawing read = load(save(dwg));
        hdxf::Entity* owner = polyline_of(read);
        hdxf_test::assert_true(owner != nullptr, "the polyline");
        owner->props.color = Color::from_aci(5);
        owner->props.layer = "LA";
        owner->props.linetype = "DASHED";
        Drawing again = load(save(read));
        const hdxf::PolylineData& out = polyline_of(again)->as_polyline();
        const std::string what = "version " + std::to_string(static_cast<int>(version));
        hdxf_test::assert_eq(5, out.vertices[0].color.aci, what + ": vertex 0 follows the color");
        hdxf_test::assert_eq(1, out.vertices[1].color.aci, what + ": vertex 1 keeps its color");
        hdxf_test::assert_eq(5, out.vertices[2].color.aci, what + ": vertex 2 follows the color");
        const hdxf::Entity& owned = *polyline_of(again);
        hdxf_test::assert_eq("LA", layer_of(out.vertices[0].layer, owned), what + ": vertex 0 follows the layer");
        hdxf_test::assert_eq("L2", layer_of(out.vertices[1].layer, owned), what + ": vertex 1 keeps its layer");
        hdxf_test::assert_eq("LA", layer_of(out.vertices[2].layer, owned), what + ": vertex 2 follows the layer");
        hdxf_test::assert_eq("DASHED", out.vertices[0].linetype, what + ": vertex 0 follows the linetype");
        hdxf_test::assert_eq("DASHED", out.vertices[1].linetype, what + ": vertex 1 follows the linetype");
        hdxf_test::assert_eq("DOTTED", out.vertices[2].linetype, what + ": vertex 2 keeps its linetype");
        hdxf_test::assert_eq(2, out.seqend_color.aci, what + ": SEQEND keeps its color");
        hdxf_test::assert_eq("LA", layer_of(out.seqend_layer, owned), what + ": SEQEND follows the layer");
    }
}

// A vertex the program changed since the file was read keeps the change, whether or not it equals what the owner gets
TEST(child_edits_kept) {
    for (DxfVersion version : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2018}) {
        Drawing dwg = made_drawing(version);
        Drawing read = load(save(dwg));
        hdxf::PolylineData& d = polyline_of(read)->as_polyline();
        polyline_of(read)->props.color = Color::from_aci(5);
        d.vertices[0].color = Color::from_aci(3);       // the program's own color
        d.vertices[1].layer = "L2";
        d.seqend_color = Color::from_aci(2);
        Drawing again = load(save(read));
        const hdxf::PolylineData& out = polyline_of(again)->as_polyline();
        const std::string what = "version " + std::to_string(static_cast<int>(version));
        hdxf_test::assert_eq(3, out.vertices[0].color.aci, what + ": the changed vertex keeps its color");
        hdxf_test::assert_eq(5, out.vertices[1].color.aci, what + ": the other follows");
        hdxf_test::assert_eq(5, out.vertices[2].color.aci, what + ": the last follows");
        hdxf_test::assert_eq("L2", layer_of(out.vertices[1].layer, *polyline_of(again)), what + ": the changed layer stays");
        hdxf_test::assert_eq(2, out.seqend_color.aci, what + ": SEQEND keeps its color");
    }
}

// Built in memory, or added to a read polyline: an empty linetype and a ByLayer color are the owner's, and the other
// properties of the owner are on the records that have them
TEST(new_children_use_owner) {
    // A polyline built in memory, and an INSERT
    {
        Drawing dwg = made_drawing(DxfVersion::R2018);
        hdxf::Entity& owner = dwg.entities().back();
        owner.props.color = Color::from_aci(1);
        owner.props.linetype = "DASHED";
        owner.props.lineweight = 50;
        owner.props.linetype_scale = 2.0;
        owner.props.transparency = hdxf::Transparency::from_alpha(153);
        dwg.add_block("B");
        hdxf::Entity& insert = dwg.add_insert("B", {0, 0, 0});
        insert.props.color = Color::from_aci(3);
        insert.props.linetype = "DOTTED";
        insert.props.lineweight = 30;
        insert.props.transparency = hdxf::Transparency::from_alpha(100);
        hdxf::InsertAttribute attribute;
        attribute.data.tag = "T1";
        attribute.data.value = "V";
        insert.as_insert().attributes.push_back(attribute);
        std::vector<uint8_t> out = save(dwg);
        auto chains = chains_of(out);
        hdxf_test::assert_eq(7, static_cast<int>(chains.size()), "POLYLINE, 3 VERTEX, SEQEND, INSERT, SEQEND");
        const Props& head = chains[0].second;
        for (size_t i = 1; i <= 4; ++i) {
            hdxf_test::assert_eq(chains[i].first, i < 4 ? "VERTEX" : "SEQEND");
            hdxf_test::assert_true(chains[i].second == head, "child " + std::to_string(i) + ": " + chains[i].second.text() +
                                                                 " is not the polyline's " + head.text());
        }
        // The INSERT's SEQEND: layer, color, linetype scale, lineweight; no linetype, no transparency
        const Props& insert_head = chains[5].second;
        const Props& insert_end = chains[6].second;
        hdxf_test::assert_eq(insert_head.layer, insert_end.layer);
        hdxf_test::assert_eq(insert_head.color, insert_end.color);
        hdxf_test::assert_eq(insert_head.weight, insert_end.weight);
        hdxf_test::assert_eq(insert_head.scale, insert_end.scale);
        hdxf_test::assert_eq(std::string(), insert_end.linetype, "the INSERT's SEQEND has no linetype");
        hdxf_test::assert_eq(std::string(), insert_end.transparency, "the INSERT's SEQEND has no transparency");

        // Only the file has the owner's values: the fields are as they were
        const hdxf::PolylineData& d = dwg.entities()[dwg.entities().size() - 2].as_polyline();
        hdxf_test::assert_true(same_color(d.vertices[0].color, Color::by_layer()), "vertex color");
        hdxf_test::assert_eq("", d.vertices[0].linetype, "vertex linetype");
        hdxf_test::assert_true(same_color(d.seqend_color, Color::by_layer()), "SEQEND color");
    }

    // A vertex added to a polyline read from a file
    for (DxfVersion version : {DxfVersion::R12, DxfVersion::R2018}) {
        Drawing dwg = made_drawing(version);
        polyline_of(dwg)->props.color = Color::from_aci(1);
        Drawing read = load(save(dwg));
        hdxf::Vertex added;
        added.position = {20, 20, 0};
        polyline_of(read)->as_polyline().vertices.push_back(added);
        std::vector<uint8_t> out = save(read);
        auto chains = chains_of(out);
        hdxf_test::assert_eq(6, static_cast<int>(chains.size()), "POLYLINE, 4 VERTEX, SEQEND");
        hdxf_test::assert_eq("1", chains[4].second.color, "the new vertex has the polyline's color");
        hdxf_test::assert_eq("1", chains[3].second.color, "and so do the others");
    }
}

// A save does not change the drawing: the next one follows the polyline as it is then, built in memory and read from a file,
// and a polyline given back what it had gives back the file it came from
TEST(each_save_follows_the_owner_as_it_is) {
    for (DxfVersion version : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2018}) {
        const std::string what = "version " + std::to_string(static_cast<int>(version));
        auto children_are = [&](const std::vector<uint8_t>& out, const std::string& color, const std::string& linetype,
                                const std::string& when) {
            auto chains = chains_of(out);
            hdxf_test::assert_eq(5, static_cast<int>(chains.size()), what + " " + when + ": POLYLINE, 3 VERTEX, SEQEND");
            for (size_t i = 1; i < chains.size(); ++i) {
                hdxf_test::assert_eq(color, chains[i].second.color, what + " " + when + ": the color of child " + std::to_string(i));
                hdxf_test::assert_eq(linetype, chains[i].second.linetype, what + " " + when + ": the linetype of child " + std::to_string(i));
            }
        };

        Drawing made = made_drawing(version);
        polyline_of(made)->props.color = Color::from_aci(1);
        polyline_of(made)->props.linetype = "DASHED";
        children_are(save(made), "1", "DASHED", "built in memory");
        polyline_of(made)->props.color = Color::from_aci(2);
        polyline_of(made)->props.linetype = "DOTTED";
        children_are(save(made), "2", "DOTTED", "built in memory, changed after a save");

        Drawing plain = made_drawing(version);
        const std::vector<uint8_t> bytes = save(plain);
        Drawing read = load(bytes);
        polyline_of(read)->props.color = Color::from_aci(3);
        children_are(save(read), "3", "", "read");
        polyline_of(read)->props.color = Color::by_layer();
        const std::vector<uint8_t> back = save(read);
        children_are(back, "", "", "read, back");
        hdxf_test::assert_true(back == bytes, what + ": the file as it was read");
    }
}

// A save that fails gives the fields back
TEST(sync_rolled_back_on_failure) {
    for (DxfVersion version : {DxfVersion::R12, DxfVersion::R2018}) {
        Drawing dwg = made_drawing(version);
        hdxf::Entity* owner = polyline_of(dwg);
        owner->props.color = Color::from_aci(1);
        owner->props.linetype = "DASHED";
        dwg.add_text({0, 0, 0}, "\xFF\xFE not UTF-8");
        std::vector<uint8_t> out = dwg.save_to_memory(DxfFormat::Ascii);
        hdxf_test::assert_true(out.empty(), "the save fails");
        const hdxf::PolylineData& d = polyline_of(dwg)->as_polyline();
        for (const auto& v : d.vertices) {
            hdxf_test::assert_true(v.color.is_by_layer(), "a vertex has the color it had");
            hdxf_test::assert_true(v.linetype.empty(), "a vertex has the linetype it had");
        }
        hdxf_test::assert_true(d.seqend_color.is_by_layer(), "SEQEND has the color it had");
        hdxf_test::assert_true(d.seqend_linetype.empty(), "SEQEND has the linetype it had");
    }
}
