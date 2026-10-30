#include "test_framework.hpp"
#include "dxf_structure.hpp"
#include "typed_corpus.hpp"
#include "hdxf.hpp"
#include "TypedRecordTypes.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Step 9.11 of phase 9: the relations of the render objects, the light list and the sun study (B19, B104): what is written
// when a light, a view, a visual style, a page setup, a background or a preset the records point at is removed, where the
// objects stand in the root dictionary, who owns them and what they are reactors of.
// ---------------------------------------------------------------------------

using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::Entity;
using hdxf::EntityType;
using hdxf::LightData;
using hdxf::Object;
using hdxf::ObjectType;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

namespace {

using Group = std::pair<int, std::string>;
using Rec = std::vector<Group>;

std::string trim(std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

std::vector<Rec> ascii_records(const std::vector<uint8_t>& bytes) {
    std::vector<Rec> out;
    std::string text(bytes.begin(), bytes.end());
    std::istringstream in(text);
    std::string code_line, value_line;
    while (std::getline(in, code_line) && std::getline(in, value_line)) {
        if (!value_line.empty() && value_line.back() == '\r') value_line.pop_back();
        int code = std::atoi(code_line.c_str());
        if (code == 0) out.emplace_back();
        if (!out.empty()) out.back().push_back({code, value_line});
    }
    return out;
}

std::vector<Group> after_marker(const Rec& r, const std::string& subclass) {
    std::vector<Group> out;
    bool in = false;
    for (const Group& g : r) {
        if (g.first == 100) {
            in = trim(g.second) == subclass;
            continue;
        }
        if (g.first >= 1000) break;
        if (in) out.push_back(g);
    }
    return out;
}

const Rec* record_named(const std::vector<Rec>& all, const std::string& name) {
    for (const Rec& r : all) if (!r.empty() && r[0].second == name) return &r;
    return nullptr;
}

std::string handle_of(const Rec& r) {
    for (const Group& g : r) if (g.first == 5) return trim(g.second);
    return {};
}

std::vector<uint8_t> save(Drawing& d, DxfFormat format, const std::string& what) {
    std::vector<uint8_t> out = d.save_to_memory(format);
    assert_true(!out.empty(), what + ": " + d.last_error());
    return out;
}

Drawing load(const std::vector<uint8_t>& bytes, const std::string& what) {
    Drawing d;
    assert_true(d.load_from_memory(bytes.data(), bytes.size()), what + ": " + d.last_error());
    return d;
}

Object make_object(ObjectType type, const std::string& handle, hdxf::ObjectData data) {
    Object o;
    o.type = type;
    o.handle = handle;
    o.owner = "0";
    o.data = std::move(data);
    return o;
}

Entity a_light(const std::string& handle, const std::string& name) {
    Entity e;
    e.type = EntityType::Light;
    e.props.handle = handle;
    LightData l;
    l.name = name;
    e.data = hdxf::Boxed<LightData>(l);
    return e;
}

Object* find_type(Drawing& d, ObjectType type) {
    for (Object& o : d.objects()) if (o.type == type) return &o;
    return nullptr;
}

void erase_object(Drawing& d, const std::string& handle) {
    auto& v = d.objects();
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Object& o) { return o.handle == handle; }), v.end());
}

void expect_no_new_dangling(const std::vector<uint8_t>& in, const std::vector<uint8_t>& out, const std::string& what) {
    std::set<std::string> before = dxf_structure::dangling_pointers(dxf_structure::records_of(in));
    for (const std::string& d : dxf_structure::dangling_pointers(dxf_structure::records_of(out))) {
        assert_true(before.count(d) > 0, what + ": a pointer that leads nowhere: " + d);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// LIGHTLIST
// ---------------------------------------------------------------------------

TEST(render_relations_a_removed_light_leaves_the_light_list_with_its_count) {
    for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
        Drawing d(DxfVersion::R2013);
        d.add_entity(a_light("7A01", "A"));
        d.add_entity(a_light("7A02", "B"));
        d.add_entity(a_light("7A03", "C"));
        d.set_light_list(hdxf::LightListData{1, {{"7A01", "A"}, {"7A02", "B"}, {"7A03", "C"}}});
        std::vector<uint8_t> bytes = save(d, format, "three lights");

        // the list as AutoCAD's reference has it: 90 version, 90 count, 5 and 1 for each
        Drawing back = load(bytes, "read");
        Object* list = find_type(back, ObjectType::LightList);
        assert_true(list != nullptr, "the list");
        assert_eq(3, static_cast<int>(list->as_light_list().lights.size()), "three members");
        // unchanged: the same bytes
        assert_true(save(back, format, "unchanged") == bytes, "unchanged");

        // the second light is removed
        auto& entities = back.entities();
        entities.erase(std::remove_if(entities.begin(), entities.end(),
                                      [](const Entity& e) { return e.props.handle == "7A02"; }), entities.end());
        std::vector<uint8_t> out = save(back, format, "a light removed");
        expect_no_new_dangling(bytes, out, "a light removed");
        Drawing again = load(out, "read again");
        Object* shorter = find_type(again, ObjectType::LightList);
        assert_true(shorter != nullptr, "the list is still there");
        assert_eq(2, static_cast<int>(shorter->as_light_list().lights.size()), "two members");
        assert_eq(std::string("7A01"), shorter->as_light_list().lights[0].light_handle, "the first member");
        assert_eq(std::string("7A03"), shorter->as_light_list().lights[1].light_handle, "the third member");
        assert_eq(std::string("C"), shorter->as_light_list().lights[1].name, "its name goes with it");
        if (format == DxfFormat::Ascii) {
            std::vector<Rec> recs = ascii_records(out);
            const Rec* r = record_named(recs, "LIGHTLIST");
            assert_true(r != nullptr, "the record");
            std::vector<Group> body = after_marker(*r, "AcDbLightList");
            assert_eq(std::string("2"), trim(body[1].second), "the count follows (90)");
            assert_eq(6, static_cast<int>(body.size()), "two members of two groups after the two 90");
        }
        // every light removed: an empty list
        auto& all = again.entities();
        all.erase(std::remove_if(all.begin(), all.end(), [](const Entity& e) { return e.type == EntityType::Light; }), all.end());
        std::vector<uint8_t> none = save(again, format, "no lights");
        Drawing empty = load(none, "no lights read");
        assert_eq(0, static_cast<int>(find_type(empty, ObjectType::LightList)->as_light_list().lights.size()), "no members");
    }
}

// The 5 of a member is a light's handle, not a handle of the record: the record keeps its own, the member's 5 is not
// taken for a sort key or an owner, and a member whose handle leads nowhere already in the file stays as it was
TEST(render_relations_the_light_list_member_handles_are_pointers_not_the_records_own) {
    Drawing d(DxfVersion::R2013);
    d.add_entity(a_light("7A11", "A"));
    d.set_light_list(hdxf::LightListData{1, {{"7A11", "A"}}});
    std::vector<uint8_t> bytes = save(d, DxfFormat::Ascii, "list");
    std::vector<Rec> recs = ascii_records(bytes);
    const Rec* r = record_named(recs, "LIGHTLIST");
    assert_true(r != nullptr, "the record");
    assert_true(handle_of(*r) != "7A11", "the record's own handle is another");
    assert_eq(std::string("7A11"), trim(after_marker(*r, "AcDbLightList")[2].second), "the member's 5 is the light's handle");

    // a file whose list names a light that is not there (AutoCAD's own files can have that): kept as it is
    std::string text(bytes.begin(), bytes.end());
    const std::string member = "  5\n7A11\n  1\nA\n";
    std::string crlf_member = "  5\r\n7A11\r\n  1\r\nA\r\n";
    size_t at = text.find(crlf_member);
    if (at == std::string::npos) at = text.find(member);
    assert_true(at != std::string::npos, "the member in the text");
    const std::string use = text.compare(at, crlf_member.size(), crlf_member) == 0 ? crlf_member : member;
    text.replace(at, use.size(), use.substr(0, use.find("7A11")) + "7A0A" + use.substr(use.find("7A11") + 4));
    Drawing read = load(std::vector<uint8_t>(text.begin(), text.end()), "dangling member");
    Object* list = find_type(read, ObjectType::LightList);
    assert_eq(std::string("7A0A"), list->as_light_list().lights[0].light_handle, "the member as it was read");
    std::vector<uint8_t> same = save(read, DxfFormat::Ascii, "dangling member");
    std::vector<uint8_t> want(text.begin(), text.end());
    std::string where;
    if (same != want) {
        size_t i = 0;
        while (i < same.size() && i < want.size() && same[i] == want[i]) ++i;
        where = " at byte " + std::to_string(i) + " [" + std::string(want.begin() + (i > 40 ? i - 40 : 0), want.begin() + std::min(want.size(), i + 40)) + "] against [" + std::string(same.begin() + (i > 40 ? i - 40 : 0), same.begin() + std::min(same.size(), i + 40)) + "]";
    }
    assert_true(same == want, "a member that led nowhere in the file is written back as it was" + where);
}

TEST(render_relations_a_light_list_has_its_own_place_and_owner) {
    Drawing d(DxfVersion::R2010);
    d.add_entity(a_light("7A21", "A"));
    Object& first = d.set_light_list(hdxf::LightListData{1, {{"7A21", "A"}}});
    const std::string handle = first.handle;
    Object& second = d.set_light_list(hdxf::LightListData{1, {}});
    assert_eq(handle, second.handle, "a second call replaces the data of the one there is");
    assert_eq(0, static_cast<int>(second.as_light_list().lights.size()), "the new data");
    std::vector<uint8_t> bytes = save(d, DxfFormat::Ascii, "list");
    Drawing back = load(bytes, "list");
    Object* list = find_type(back, ObjectType::LightList);
    assert_true(list != nullptr, "the list");
    const Object* root = nullptr;
    for (const Object& o : back.objects()) if (o.type == ObjectType::Dictionary && o.owner == "0") root = &o;
    assert_true(root != nullptr, "the root dictionary");
    bool entry = false;
    for (const auto& e : root->as_dictionary().entries) entry = entry || (e.name == "ACAD_LIGHT" && e.handle == list->handle);
    assert_true(entry, "ACAD_LIGHT names the list");
    assert_eq(root->handle, list->owner, "the root dictionary owns it");
    assert_true(std::find(list->reactors.begin(), list->reactors.end(), root->handle) != list->reactors.end(), "and is its reactor");
}

// ---------------------------------------------------------------------------
// SUNSTUDY: page setup, view, visual style and text style
// ---------------------------------------------------------------------------

TEST(render_relations_a_removed_view_or_style_is_none_in_the_sun_study) {
    for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
        Drawing d(DxfVersion::R2013);
        hdxf::View view;
        view.name = "SUNVIEW";
        view.handle = "7B01";
        d.views().push_back(view);
        d.objects().push_back(make_object(ObjectType::PlotSettings, "7B02", hdxf::PlotSettings()));
        d.objects().push_back(make_object(ObjectType::VisualStyle, "7B03", hdxf::Boxed<hdxf::VisualStyleData>()));
        hdxf::SunStudyData s;
        s.name = "Study";
        s.page_setup_handle = "7B02";
        s.view_handle = "7B01";
        s.visual_style_handle = "7B03";
        hdxf::TextStyle style;
        style.name = "SUNSTYLE";
        style.handle = "7B05";
        d.text_styles().push_back(style);
        s.text_style_handle = "7B05";
        d.objects().push_back(make_object(ObjectType::SunStudy, "7B04", hdxf::Boxed<hdxf::SunStudyData>(s)));
        std::vector<uint8_t> bytes = save(d, format, "study");

        Drawing back = load(bytes, "study");
        Object* study = find_type(back, ObjectType::SunStudy);
        assert_true(study != nullptr, "the study");
        assert_eq(std::string("7B01"), study->as_sun_study().view_handle, "the view");
        assert_eq(std::string("7B03"), study->as_sun_study().visual_style_handle, "the style");
        assert_true(save(back, format, "unchanged") == bytes, "unchanged: the same bytes");

        // the style is removed: its pointer is 0, the others stay
        erase_object(back, "7B03");
        std::vector<uint8_t> out = save(back, format, "style removed");
        expect_no_new_dangling(bytes, out, "style removed");
        Drawing again = load(out, "style removed");
        const hdxf::SunStudyData& a = find_type(again, ObjectType::SunStudy)->as_sun_study();
        assert_eq(std::string("0"), a.visual_style_handle, "the style is none");
        assert_eq(std::string("7B01"), a.view_handle, "the view stays");
        assert_eq(std::string("7B02"), a.page_setup_handle, "the page setup stays");
        assert_true(a.text_style_handle.has_value(), "the text style is still given");

        // the view is removed
        auto& views = again.views();
        views.erase(std::remove_if(views.begin(), views.end(), [](const hdxf::View& v) { return v.handle == "7B01"; }), views.end());
        std::vector<uint8_t> out2 = save(again, format, "view removed");
        expect_no_new_dangling(out, out2, "view removed");
        Drawing third = load(out2, "view removed");
        assert_eq(std::string("0"), find_type(third, ObjectType::SunStudy)->as_sun_study().view_handle, "the view is none");

        // the page setup too
        erase_object(third, "7B02");
        Drawing fourth = load(save(third, format, "page setup removed"), "page setup removed");
        const hdxf::SunStudyData& f = find_type(fourth, ObjectType::SunStudy)->as_sun_study();
        assert_eq(std::string("0"), f.page_setup_handle, "the page setup is none");
        assert_eq(2, static_cast<int>(f.hours.size() + 2), "the rest of the study is as it was");
    }
}

TEST(render_relations_a_sun_study_built_over_a_removed_style_warns_and_writes_none) {
    Drawing d(DxfVersion::R2010);
    hdxf::SunStudyData s;
    s.name = "Study";
    s.visual_style_handle = "7B33";          // there is no such record
    d.objects().push_back(make_object(ObjectType::SunStudy, "7B34", hdxf::Boxed<hdxf::SunStudyData>(s)));
    std::vector<uint8_t> bytes = save(d, DxfFormat::Ascii, "study");
    bool warned = false;
    for (const std::string& w : d.warnings()) warned = warned || w.find("visual style") != std::string::npos;
    assert_true(warned, "a warning that names the visual style");
    Drawing back = load(bytes, "study");
    assert_eq(std::string("0"), find_type(back, ObjectType::SunStudy)->as_sun_study().visual_style_handle, "none");
}

// ---------------------------------------------------------------------------
// The IBL environment and the root dictionary's render entries
// ---------------------------------------------------------------------------

TEST(render_relations_a_removed_custom_background_is_none_in_the_ibl_environment) {
    for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
        Drawing d(DxfVersion::R2013);
        hdxf::RapidRtRenderEnvironmentData first;
        first.enabled = true;
        Object& a = d.set_rapidrt_render_environment(first);
        const std::string a_handle = a.handle;
        hdxf::RapidRtRenderEnvironmentData second;
        second.ibl_file = "sky.hdr";
        second.rotation = 12.5;
        // the environment there is is replaced by data, the background it was stays only if it is given
        second.background_handle = a_handle;
        d.objects().push_back(make_object(ObjectType::RapidRtRenderEnvironment, "7C02", second));
        std::vector<uint8_t> bytes = save(d, format, "environments");
        Drawing back = load(bytes, "environments");
        const Object* b = back.find_object("7C02");
        assert_true(b != nullptr && b->type == ObjectType::RapidRtRenderEnvironment, "the second environment");
        assert_eq(a_handle, b->as_rapidrt_environment().background_handle, "it points at the first");
        assert_true(save(back, format, "unchanged") == bytes, "unchanged");

        erase_object(back, a_handle);
        std::vector<uint8_t> out = save(back, format, "background removed");
        expect_no_new_dangling(bytes, out, "background removed");
        Drawing again = load(out, "background removed");
        assert_eq(std::string("0"), again.find_object("7C02")->as_rapidrt_environment().background_handle, "none (0)");
        assert_eq(12.5, again.find_object("7C02")->as_rapidrt_environment().rotation, "the rest is as it was");
        bool warned = false;
        for (const std::string& w : back.warnings()) warned = warned || w.find("custom background") != std::string::npos;
        assert_true(warned, "a warning names the custom background");
    }
}

TEST(render_relations_the_root_dictionary_names_the_render_objects_and_follows_their_removal) {
    Drawing d(DxfVersion::R2013);
    hdxf::MentalRayRenderSettingsData m;
    m.base.name = "Medium";
    Object& mental = d.set_mentalray_render_settings(m);
    const std::string mental_handle = mental.handle;
    d.set_render_environment(hdxf::RenderEnvironmentData());
    d.set_render_global(hdxf::RenderGlobalData());
    std::vector<uint8_t> bytes = save(d, DxfFormat::Ascii, "render objects");
    Drawing back = load(bytes, "render objects");
    const Object* root = nullptr;
    for (const Object& o : back.objects()) if (o.type == ObjectType::Dictionary && o.owner == "0") root = &o;
    assert_true(root != nullptr, "the root dictionary");
    std::map<std::string, std::string> entries;
    for (const auto& e : root->as_dictionary().entries) entries[e.name] = e.handle;
    assert_true(entries.count("ACAD_RENDER_ACTIVE_SETTINGS") > 0, "the MentalRay preset: ACAD_RENDER_ACTIVE_SETTINGS (as AutoCAD's files)");
    assert_true(entries.count("ACAD_RENDER_ENVIRONMENT") > 0, "ACAD_RENDER_ENVIRONMENT");
    assert_true(entries.count("ACAD_RENDER_GLOBAL") > 0, "ACAD_RENDER_GLOBAL");
    const Object* mental_back = back.find_object(entries["ACAD_RENDER_ACTIVE_SETTINGS"]);
    assert_true(mental_back != nullptr && mental_back->type == ObjectType::MentalRayRenderSettings, "it is the preset");
    assert_eq(root->handle, mental_back->owner, "owned by the root dictionary");
    // the entries are sorted as AutoCAD's dictionaries are
    std::vector<std::string> names;
    for (const auto& e : root->as_dictionary().entries) names.push_back(e.name);
    std::vector<std::string> sorted = names;
    std::sort(sorted.begin(), sorted.end());
    assert_true(names == sorted, "the root dictionary stays sorted");

    // the preset is removed: its entry goes (B19), nothing leads nowhere
    erase_object(back, entries["ACAD_RENDER_ACTIVE_SETTINGS"]);
    std::vector<uint8_t> out = save(back, DxfFormat::Ascii, "preset removed");
    expect_no_new_dangling(bytes, out, "preset removed");
    Drawing again = load(out, "preset removed");
    bool still = false;
    for (const Object& o : again.objects()) {
        if (o.type != ObjectType::Dictionary || o.owner != "0") continue;
        for (const auto& e : o.as_dictionary().entries) still = still || e.name == "ACAD_RENDER_ACTIVE_SETTINGS";
    }
    assert_false(still, "the entry follows the removal");
    (void)mental_handle;
}

TEST(render_relations_a_standard_preset_of_autocad_can_be_removed) {
    const std::string name = "acad_2013_dxf_modeldoc_views.dxf";
    std::vector<uint8_t> in = hdxf_test::read_file_bytes(hdxf_test::source_path("doc/facit/" + name));
    if (in.empty()) hdxf_test::skip("missing doc/facit/" + name);
    Drawing d = load(in, name);
    std::string handle;
    for (Object& o : d.objects()) {
        if (o.type == ObjectType::MentalRayRenderSettings) { handle = o.handle; break; }
    }
    assert_true(!handle.empty(), "a MentalRay preset");
    erase_object(d, handle);
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "removed");
    expect_no_new_dangling(in, out, "a preset removed");
    Drawing again = load(out, "removed");
    assert_true(again.find_object(handle) == nullptr, "gone");
    int presets = 0;
    for (const Object& o : again.objects()) presets += o.type == ObjectType::MentalRayRenderSettings;
    assert_eq(0, presets, "the file had the one active preset");
}
