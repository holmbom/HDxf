#include "assoc_test_support.hpp"
#include <iostream>

// The ACDBASSOC network in a whole drawing (step 9.10; plan B19, B104, B110): network, action, body, parameters,
// dependencies and geometry, in both directions, with removals taken one at a time. The facit is AutoCAD's own: the
// drawing doc/facit/acad_<version>_dxf_assoc_core.dxf (fas9_assoc_core.scr) and the same drawing after AutoCAD erased
// the second extrusion of a line that two actions share.

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;
using namespace assoc_test;

namespace {

std::filesystem::path facit(const std::string& name) { return hdxf_test::source_path("doc/facit/" + name); }

const char* const kVersions[] = {"2000", "2004", "2007", "2010", "2013", "2018"};

std::string core_file(const std::string& version, const std::string& change = std::string()) {
    return "acad_" + version + "_dxf_assoc_core" + (change.empty() ? "" : "_" + change) + ".dxf";
}

bool have_facit() { return std::filesystem::exists(facit(core_file("2018"))) && std::filesystem::exists(facit(core_file("2018", "surface_erased"))); }

// The shared line of the drawing and what leads from it: the line (the entity AutoCAD made two extrusions of), its two
// dependencies, the second one's action and the surface that action writes
struct Shared {
    std::string line, first_dependency, second_dependency, second_action, surface;
};

Shared find_shared(const hdxf::Drawing& d) {
    Shared s;
    for (const auto& e : d.entities()) {
        if (e.type != hdxf::EntityType::Line) continue;
        std::vector<std::string> deps;
        for (const auto& r : e.props.reactors) {
            const hdxf::Object* o = find_object(d, r);
            if (o && o->type == hdxf::ObjectType::AssocDependency) deps.push_back(r);
        }
        if (deps.size() == 2) {
            s.line = e.props.handle;
            s.first_dependency = deps[0];
            s.second_dependency = deps[1];
            break;
        }
    }
    assert_true(!s.line.empty(), "the drawing has a line two dependencies point to");
    s.second_action = find_object(d, s.second_dependency)->owner;
    for (const auto& o : d.objects()) {
        if (o.type == hdxf::ObjectType::AssocDependency && o.owner == s.second_action &&
            o.as_assoc_dependency().dependency.order == INT32_MIN) {
            s.surface = o.as_assoc_dependency().dependency.dependent_on_object;
        }
    }
    assert_true(!s.surface.empty() && s.surface != "0", "the second action writes a surface");
    return s;
}

void erase_entity(hdxf::Drawing& d, const std::string& handle) {
    auto& list = d.entities();
    auto it = std::find_if(list.begin(), list.end(), [&](const hdxf::Entity& e) { return e.props.handle == handle; });
    assert_true(it != list.end(), "no entity " + handle);
    list.erase(it);
}

std::string record_of(const std::vector<uint8_t>& bytes, const hdxf::Drawing& d, const std::string& handle) {
    if (const hdxf::Object* o = find_object(d, handle)) return ascii_record(bytes, dxf_name_of(o->type), handle);
    for (const auto& e : d.entities()) {
        if (e.props.handle == handle) return ascii_record(bytes, e.type == hdxf::EntityType::Line ? "LINE" : "", handle);
    }
    return {};
}

} // namespace

TEST(assoc_relations_erasing_one_of_two_extrusions_of_a_shared_line_is_what_autocad_did) {
    if (!have_facit()) hdxf_test::skip("no doc/facit/acad_*_dxf_assoc_core*.dxf");
    for (const char* version : kVersions) {
        std::vector<uint8_t> original = bytes_of(facit(core_file(version)));
        std::vector<uint8_t> erased = bytes_of(facit(core_file(version, "surface_erased")));
        hdxf::Drawing d;
        assert_true(load_bytes(d, original), std::string("load ") + version);
        Shared s = find_shared(d);
        const size_t actions = count_objects(d, hdxf::ObjectType::AssocAction);
        const size_t dependencies = count_objects(d, hdxf::ObjectType::AssocDependency);
        // The surface and its action go: what is owned by the action goes with it, a shared line stays
        erase_entity(d, s.surface);
        erase_object(d, s.second_action);
        std::vector<uint8_t> ours = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!ours.empty(), std::string(version) + ": not written: " + d.last_error());
        hdxf::Drawing r = reload(ours);
        assert_eq(static_cast<int>(actions) - 1, static_cast<int>(count_objects(r, hdxf::ObjectType::AssocAction)), std::string(version) + ": one action less");
        assert_true(count_objects(r, hdxf::ObjectType::AssocDependency) < dependencies, std::string(version) + ": its dependencies went");
        assert_true(find_object(r, s.second_action) == nullptr && find_object(r, s.second_dependency) == nullptr, std::string(version) + ": nothing of it is left");
        // The records AutoCAD changed by erasing it are the ones HDxf changed, and they are the same: the network without the
        // action (its count follows), the first dependency without its chain pointer, the line with one reactor less
        std::vector<uint8_t> ours_text = ours;
        std::vector<std::string> checked;
        const hdxf::Object* second = find_object(d, s.second_action);
        (void)second;
        for (const auto& o : r.objects()) {
            if (o.type != hdxf::ObjectType::AssocNetwork && o.handle != s.first_dependency) continue;
            std::string expected = ascii_record(erased, dxf_name_of(o.type), o.handle);
            std::string got = ascii_record(ours_text, dxf_name_of(o.type), o.handle);
            assert_true(!expected.empty() && !got.empty(), std::string(version) + ": " + o.handle + " is in both files");
            assert_eq(std::string(), record_difference(expected, got), std::string(version) + ": " + dxf_name_of(o.type) + " " + o.handle + " as AutoCAD has it");
            checked.push_back(o.handle);
        }
        std::string line_expected = ascii_record(erased, "LINE", s.line);
        std::string line_got = ascii_record(ours_text, "LINE", s.line);
        assert_true(!line_expected.empty() && !line_got.empty(), std::string(version) + ": the line");
        assert_eq(std::string(), record_difference(line_expected, line_got), std::string(version) + ": the line's reactors as AutoCAD has them");
        assert_true(checked.size() >= 2, std::string(version) + ": the network and the dependency were compared");
    }
}

// Step 9.15 (the second gap of 9.10): AutoCAD erases the actions that stood on the erased profile (profile_erased)
TEST(assoc_relations_removing_the_shared_line_removes_both_actions_that_stood_on_it_as_autocad_did) {
    if (!have_facit()) hdxf_test::skip("no doc/facit/acad_*_dxf_assoc_core*.dxf");
    for (const char* version : {"2010", "2018"}) {
        std::vector<uint8_t> original = bytes_of(facit(core_file(version)));
        hdxf::Drawing d;
        assert_true(load_bytes(d, original), "load");
        Shared s = find_shared(d);
        const size_t actions = count_objects(d, hdxf::ObjectType::AssocAction);
        erase_entity(d, s.line);
        std::vector<uint8_t> ours = d.save_to_memory(DxfFormat::Ascii);
        hdxf::Drawing r = reload(ours);
        hdxf::Drawing autocad;
        assert_true(load_file(autocad, facit(core_file(version, "profile_erased"))), "load AutoCAD's");
        assert_eq(static_cast<int>(count_objects(autocad, hdxf::ObjectType::AssocAction)), static_cast<int>(count_objects(r, hdxf::ObjectType::AssocAction)),
                  std::string(version) + ": the actions AutoCAD left");
        assert_true(count_objects(r, hdxf::ObjectType::AssocAction) < actions, std::string(version) + ": the two extrusions' actions went");
        assert_true(find_object(r, s.first_dependency) == nullptr && find_object(r, s.second_dependency) == nullptr,
                    std::string(version) + ": their dependencies went with them");
        assert_eq(static_cast<int>(count_objects(autocad, hdxf::ObjectType::AssocDependency)), static_cast<int>(count_objects(r, hdxf::ObjectType::AssocDependency)),
                  std::string(version) + ": the dependencies AutoCAD left");
    }
}

TEST(assoc_relations_removing_a_network_takes_its_actions_and_what_they_own_with_it) {
    if (!have_facit()) hdxf_test::skip("no doc/facit/acad_*_dxf_assoc_core*.dxf");
    std::vector<uint8_t> original = bytes_of(facit(core_file("2018")));
    hdxf::Drawing d;
    assert_true(load_bytes(d, original), "load");
    // the network that owns the actions
    std::string network;
    for (const auto& o : d.objects()) {
        if (o.type == hdxf::ObjectType::AssocNetwork && o.as_assoc_network().actions.size() > 2) network = o.handle;
    }
    assert_true(!network.empty(), "a network with actions");
    const size_t geometry = d.entities().size();
    erase_object(d, network);
    std::vector<uint8_t> ours = d.save_to_memory(DxfFormat::Ascii);
    hdxf::Drawing r = reload(ours);
    assert_eq(0, static_cast<int>(count_objects(r, hdxf::ObjectType::AssocAction)), "no action");
    assert_eq(0, static_cast<int>(count_objects(r, hdxf::ObjectType::AssocDependency)), "no dependency");
    assert_eq(0, static_cast<int>(count_objects(r, hdxf::ObjectType::AssocEdgeActionParam)), "no edge parameter");
    assert_eq(0, static_cast<int>(count_objects(r, hdxf::ObjectType::AssocPathActionParam)), "no path parameter");
    assert_eq(0, static_cast<int>(count_objects(r, hdxf::ObjectType::AssocExtrudedSurfaceActionBody)), "no body");
    assert_eq(static_cast<int>(geometry), static_cast<int>(r.entities().size()), "the lines and the surfaces stay");
    // The outer network pointed at the one that is gone (330): its list is empty and its count follows
    bool outer = false;
    for (const auto& o : r.objects()) {
        if (o.type != hdxf::ObjectType::AssocNetwork) continue;
        outer = true;
        assert_eq(0, static_cast<int>(o.as_assoc_network().actions.size()), "the outer network's list lost the network");
    }
    assert_true(outer, "the outer network stays");
}

TEST(assoc_relations_a_parameter_or_a_dependency_is_removed_alone) {
    if (!have_facit()) hdxf_test::skip("no doc/facit/acad_*_dxf_assoc_core*.dxf");
    // 2018: the parameters are in the action; 2010: in the action body
    for (const char* version : {"2010", "2018"}) {
        hdxf::Drawing d;
        assert_true(load_file(d, facit(core_file(version))), "load");
        Shared s = find_shared(d);
        // the action's path parameter
        // the parameter is owned by the action (2013) or by its action body (before)
        std::string body_handle = find_object(d, s.second_action)->as_assoc_action().action.action_body;
        std::string param;
        for (const auto& o : d.objects()) {
            if (o.type == hdxf::ObjectType::AssocPathActionParam && (o.owner == s.second_action || o.owner == body_handle)) param = o.handle;
        }
        assert_true(!param.empty(), std::string(version) + ": the action has a path parameter");
        auto params_of = [&](const hdxf::Drawing& drawing) -> size_t {
            const hdxf::Object* action = find_object(drawing, s.second_action);
            assert_true(action != nullptr, "the action");
            if (action->as_assoc_action().action.version >= 2) return action->as_assoc_action().action.params.size();
            const hdxf::Object* body = find_object(drawing, action->as_assoc_action().action.action_body);
            assert_true(body != nullptr && body->as_assoc_extruded_surface_action_body().head.param_based.has_value(), "the body has its parameters");
            return body->as_assoc_extruded_surface_action_body().head.param_based->params.size();
        };
        const size_t before = params_of(d);
        assert_eq(1, static_cast<int>(before), std::string(version) + ": one parameter");
        const size_t actions = count_objects(d, hdxf::ObjectType::AssocAction);
        const size_t deps = count_objects(d, hdxf::ObjectType::AssocDependency);
        erase_object(d, param);
        std::vector<uint8_t> ours = d.save_to_memory(DxfFormat::Ascii);
        hdxf::Drawing r = reload(ours);
        assert_eq(0, static_cast<int>(params_of(r)), std::string(version) + ": the list lost the parameter, its count follows");
        assert_eq(static_cast<int>(actions), static_cast<int>(count_objects(r, hdxf::ObjectType::AssocAction)), std::string(version) + ": the actions stay");
        assert_eq(static_cast<int>(deps), static_cast<int>(count_objects(r, hdxf::ObjectType::AssocDependency)), std::string(version) + ": the dependencies stay");
        // what the parameter owned (the edge parameter) went with it
        for (const auto& o : r.objects()) {
            assert_true(!(o.type == hdxf::ObjectType::AssocEdgeActionParam && o.owner == param), std::string(version) + ": its child went");
        }
        // a dependency alone: the action's list and the chain follow
        hdxf::Drawing e;
        assert_true(load_file(e, facit(core_file(version))), "load");
        erase_object(e, s.first_dependency);
        std::vector<uint8_t> without = e.save_to_memory(DxfFormat::Ascii);
        hdxf::Drawing w = reload(without);
        const hdxf::Object* second = find_object(w, s.second_dependency);
        assert_true(second != nullptr, "the second dependency stays");
        assert_eq(std::string("0"), second->as_assoc_dependency().dependency.previous, std::string(version) + ": its previous is none now");
        bool listed = false;
        for (const auto& o : w.objects()) {
            if (o.type != hdxf::ObjectType::AssocAction) continue;
            for (const std::string& h : o.as_assoc_action().action.dependencies) listed = listed || h == s.first_dependency;
        }
        assert_false(listed, std::string(version) + ": no action lists the dependency");
    }
}

TEST(assoc_relations_a_link_that_was_broken_in_the_file_is_kept_and_the_rest_is_cleaned) {
    if (!have_facit()) hdxf_test::skip("no doc/facit/acad_*_dxf_assoc_core*.dxf");
    std::vector<uint8_t> original = bytes_of(facit(core_file("2018")));
    hdxf::Drawing probe;
    assert_true(load_bytes(probe, original), "load");
    Shared s = find_shared(probe);
    // the first dependency's pointer to the line made a handle that no record has
    std::string text(original.begin(), original.end());
    assert_true(!ascii_record(original, "ACDBASSOCDEPENDENCY", s.first_dependency).empty(), "the dependency is in the file");
    // the file has the line ending the writer was given (CRLF)
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    size_t at = text.find("AcDbAssocDependency", text.find("  5" + eol + s.first_dependency + eol));
    assert_true(at != std::string::npos, "the dependency's subclass");
    const std::string pointer = "330" + eol + s.line + eol;
    size_t in_record = text.find(pointer, at);
    assert_true(in_record != std::string::npos, "the pointer to the line");
    std::string broken = text;
    broken.replace(in_record + 3 + eol.size(), s.line.size(), "FFFE");
    hdxf::Drawing d;
    assert_true(d.load_from_memory(reinterpret_cast<const uint8_t*>(broken.data()), broken.size()), "readable");
    const hdxf::Object* dep = find_object(d, s.first_dependency);
    assert_true(dep != nullptr && dep->as_assoc_dependency().dependency.dependent_on_object == "FFFE", "the broken pointer is read as it is");
    // written unchanged it stays, and so does it when something else is removed
    std::vector<uint8_t> same = d.save_to_memory(DxfFormat::Ascii);
    assert_true(same == std::vector<uint8_t>(broken.begin(), broken.end()), "written as read");
    erase_entity(d, s.surface);
    erase_object(d, s.second_action);
    std::vector<uint8_t> after = d.save_to_memory(DxfFormat::Ascii);
    hdxf::Drawing r = reload(after);
    const hdxf::Object* kept = find_object(r, s.first_dependency);
    assert_true(kept != nullptr, "the dependency stays");
    assert_eq(std::string("FFFE"), kept->as_assoc_dependency().dependency.dependent_on_object, "the link that led nowhere in the file is kept (B19)");
}
