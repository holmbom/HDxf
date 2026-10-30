#include "assoc_test_support.hpp"
#include <iostream>

// What AutoCAD removes with the geometry an action stands on (step 9.15; the second gap 9.10 left): doc/facit/
// fas9_assoc_extended.scr erases the profile, cutting curve, source, patch curve, network curve, solid, wall or constrained line
// of one action each, in 2000 … 2018. HDxf erases the same entities from the drawing as read and must leave the same associative
// records as AutoCAD's own file of that erase (B19).

using hdxf::DxfFormat;
using hdxf_test::assert_true;
using namespace assoc_test;

namespace {

std::filesystem::path facit(const std::string& name) { return hdxf_test::source_path("doc/facit/" + name); }

bool is_assoc_object(const hdxf::Object& o) {
    const hdxf::internal::ObjectCodec* c = hdxf::internal::TypedRegistry::instance().find_object(o.type);
    return c && (c->step == 10 || c->step == 15);
}

std::set<std::string> assoc_handles(const hdxf::Drawing& d) {
    std::set<std::string> out;
    // (the managers are numbered afresh by every save, and so are the restore actions that the saver of a version 1 file (2000 …
    // 2010) makes under the root dictionary: the comparison is of the actions of the drawing's network)
    std::set<std::string> saver_actions;
    std::string root;
    for (const auto& o : d.objects()) {
        if (o.type == hdxf::ObjectType::Dictionary && (o.owner == "0" || o.owner.empty())) { root = o.handle; break; }
    }
    for (const auto& o : d.objects()) {
        if (o.type == hdxf::ObjectType::AssocAction && !root.empty() && o.owner == root) saver_actions.insert(o.handle);
    }
    for (const auto& o : d.objects()) {
        if (!is_assoc_object(o) || o.type == hdxf::ObjectType::AssocPersSubentManager || o.type == hdxf::ObjectType::PersSubentManager) continue;
        if (saver_actions.count(o.handle) || saver_actions.count(o.owner)) continue;
        out.insert(o.handle);
    }
    return out;
}

std::set<std::string> entity_handles(const hdxf::Drawing& d) {
    std::set<std::string> out;
    for (const auto& e : d.entities()) out.insert(e.props.handle);
    return out;
}

} // namespace

TEST(assoc_removal_erasing_geometry_removes_the_actions_that_stood_on_it_as_autocad_did) {
    static const char* const changes[] = {"trim_curve_erased", "offset_source_erased", "patch_curve_erased", "network_curve_erased",
                                          "solid_erased", "fillet_wall_erased", "constrained_line_erased"};
    static const char* const versions[] = {"2000", "2007", "2010", "2013", "2018"};
    if (!std::filesystem::exists(facit("acad_2018_dxf_assoc_extended.dxf"))) hdxf_test::skip("no doc/facit/acad_*_dxf_assoc_extended*.dxf");
    int pairs = 0;
    std::vector<std::string> failures;
    for (const char* version : versions) {
        const std::string base_name = std::string("acad_") + version + "_dxf_assoc_extended.dxf";
        std::vector<uint8_t> base_bytes = bytes_of(facit(base_name));
        hdxf::Drawing base;
        assert_true(load_bytes(base, base_bytes), "load " + base_name);
        for (const char* change : changes) {
            const std::string change_name = std::string("acad_") + version + "_dxf_assoc_extended_" + change + ".dxf";
            hdxf::Drawing autocad;
            assert_true(load_file(autocad, facit(change_name)), "load " + change_name);
            hdxf::Drawing d;
            assert_true(load_bytes(d, base_bytes), "load " + base_name);
            // the geometry AutoCAD's ERASE took; a dimensional constraint's DIMENSION (on *ADSK_CONSTRAINTS) is left to HDxf,
            // which drops it with its dependency as AutoCAD does
            std::set<std::string> kept = entity_handles(autocad);
            auto& list = d.entities();
            list.erase(std::remove_if(list.begin(), list.end(), [&](const hdxf::Entity& e) {
                           return !kept.count(e.props.handle) && !(e.type == hdxf::EntityType::Dimension && e.props.layer == "*ADSK_CONSTRAINTS");
                       }),
                       list.end());
            std::vector<uint8_t> ours = d.save_to_memory(DxfFormat::Ascii);
            assert_true(!ours.empty(), change_name + ": not written: " + d.last_error());
            hdxf::Drawing r = reload(ours);
            std::set<std::string> have = assoc_handles(r);
            std::set<std::string> want = assoc_handles(autocad);
            for (const auto& h : entity_handles(r)) have.insert("entity " + h);
            for (const auto& h : entity_handles(autocad)) want.insert("entity " + h);
            ++pairs;
            std::string extra, missing;
            for (const auto& h : have) if (!want.count(h)) extra += " " + h;
            for (const auto& h : want) if (!have.count(h)) missing += " " + h;
            if (!extra.empty() || !missing.empty()) {
                failures.push_back(std::string(version) + " " + change + ": HDxf keeps" + (extra.empty() ? " nothing more" : extra) +
                                   "; AutoCAD keeps" + (missing.empty() ? " nothing more" : missing));
            }
        }
    }
    std::cout << "assoc removal: " << pairs << " AutoCAD pairs\n";
    std::string message;
    for (size_t i = 0; i < failures.size() && i < 12; ++i) message += "\n  " + failures[i];
    assert_true(failures.empty(), std::to_string(failures.size()) + " pairs differ:" + message);
}
