#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "dxf_structure.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The reference's additional types (phase 6, step 6.7 — plan B78): the coordination model and its
// definition (NAVISWORKSMODELDEF in ACAD_BIM_DEFINITIONS), the RAPIDRT render presets
// (RAPIDRTRENDERSETTINGS) and the IBL environment (RAPIDRTRENDERENVIRONMENT).
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

std::vector<uint8_t> save(hdxf::Drawing& dwg, hdxf::DxfFormat format = hdxf::DxfFormat::Ascii) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), "save: " + dwg.last_error());
    return out;
}

hdxf::Drawing load(const std::vector<uint8_t>& data) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(data.data(), data.size()), dwg.last_error());
    return dwg;
}

// The first record of a type (the CLASS record when `class_name` is true)
Record find_type(const std::vector<Record>& records, const std::string& name, bool class_name = false) {
    for (const auto& r : records) {
        if (class_name) {
            if (r[0].second == "CLASS" && r.size() > 1 && r[1].second == name) return r;
        } else if (r[0].second == name) {
            return r;
        }
    }
    return {};
}

// The groups of a record after the marker of `subclass`, until the next marker
Record groups_in(const Record& r, const std::string& subclass) {
    Record out;
    bool in = false;
    for (const auto& g : r) {
        if (g.first == 100) { in = g.second == subclass; continue; }
        if (g.first >= 1000) break;
        if (in) out.push_back(g);
    }
    return out;
}

std::vector<int> codes_of(const Record& r) {
    std::vector<int> out;
    for (const auto& g : r) out.push_back(g.first);
    return out;
}

std::vector<std::string> values_of(const Record& r, int code) {
    std::vector<std::string> out;
    for (const auto& g : r) if (g.first == code) out.push_back(g.second);
    return out;
}

const std::array<double, 16> kMatrix = {0, -1, 0, 10, 1, 0, 0, 20, 0, 0, 1, 30, 0, 0, 0, 1};

hdxf::Drawing model_drawing(hdxf::DxfVersion version = hdxf::DxfVersion::R2018) {
    hdxf::Drawing dwg(version);
    dwg.add_coordination_model("C:\\models\\plant.nwd", {0, 0, 0}, {2, 3, 4}, kMatrix, 25.4);
    return dwg;
}

hdxf::RapidRtRenderSettingsData preset() {
    hdxf::RapidRtRenderSettingsData d;
    d.base_version = 2;
    d.name = "F\xC3\xB6rhandsgranskning";
    d.materials = true;
    d.texture_sampling = true;
    d.back_faces = false;
    d.shadows = true;
    d.preview_file = "preview.png";
    d.description = "Renders for 33 minutes";
    d.display_index = 4;
    d.version = 3;
    d.duration_type = hdxf::RenderDurationType::ByTime;
    d.levels = 7;
    d.duration = 33;
    d.accuracy = hdxf::RenderAccuracy::High;
    d.filter = hdxf::SamplingFilter::Lanczos;
    d.filter_width = 2.5;
    d.filter_height = 4.5;
    d.predefined = true;
    return d;
}

} // namespace

// The model's groups in the order AutoCAD's core accepts (70 after the marker, 340, 16 + 1 values
// of code 40) and the definition's (70, 1, 290, min, max, 290), in ASCII and binary; the classes
TEST(reference_2018_coordination_model_groups) {
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg = model_drawing();
        std::vector<uint8_t> data = save(dwg, format);
        if (format == hdxf::DxfFormat::Ascii) dxf_structure::check_structure(data, "coordination model");
        std::vector<Record> recs = records_of(data);

        Record model = find_type(recs, "COORDINATION_MODEL");
        hdxf_test::assert_false(model.empty(), "the entity is written");
        Record body = groups_in(model, "AcDbNavisworksModel");
        std::vector<int> expect = {70, 340};
        expect.insert(expect.end(), 17, 40);
        hdxf_test::assert_true(codes_of(body) == expect, "70, 340 and 17 x 40");
        std::vector<std::string> numbers = values_of(body, 40);
        hdxf_test::assert_eq(std::atof(numbers[3].c_str()), 10.0, "translation x is the fourth value");
        hdxf_test::assert_eq(std::atof(numbers[7].c_str()), 20.0, "translation y");
        hdxf_test::assert_eq(std::atof(numbers[16].c_str()), 25.4, "the unit factor is the seventeenth value");
        hdxf_test::assert_true(body[1].second != "0" && !body[1].second.empty(), "the definition is pointed to");

        Record def = find_type(recs, "NAVISWORKSMODELDEF");
        hdxf_test::assert_false(def.empty(), "the definition is written");
        Record def_body = groups_in(def, "AcDbNavisworksModelDef");
        hdxf_test::assert_true(codes_of(def_body) == std::vector<int>({70, 1, 290, 10, 20, 30, 11, 21, 31, 290}),
                               "70, path, load, min, max, host visibility");
        hdxf_test::assert_eq(def_body[1].second, "C:\\models\\plant.nwd", "path");
        hdxf_test::assert_eq(def_body[2].second, "0", "load state");
        hdxf_test::assert_eq(def_body[9].second, "1", "host visibility");

        // The definition is in ACAD_BIM_DEFINITIONS, which the root dictionary names
        std::string def_handle;
        for (const auto& g : def) if (g.first == 5) { def_handle = g.second; break; }
        bool entered = false;
        std::string bim;
        for (const auto& r : recs) {
            if (r[0].second != "DICTIONARY") continue;
            for (size_t i = 0; i + 1 < r.size(); ++i) {
                if (r[i].first == 3 && r[i].second == "ACAD_BIM_DEFINITIONS") bim = r[i + 1].second;
            }
        }
        hdxf_test::assert_false(bim.empty(), "the root dictionary names ACAD_BIM_DEFINITIONS");
        for (const auto& r : recs) {
            if (r[0].second != "DICTIONARY" || values_of(r, 5).empty() || values_of(r, 5)[0] != bim) continue;
            entered = values_of(r, 350) == std::vector<std::string>{def_handle};
        }
        hdxf_test::assert_true(entered, "the definition is the dictionary's entry");

        Record model_class = find_type(recs, "COORDINATION_MODEL", true);
        Record def_class = find_type(recs, "NAVISWORKSMODELDEF", true);
        hdxf_test::assert_false(model_class.empty() || def_class.empty(), "both classes");
        hdxf_test::assert_eq(model_class[2].second, "AcDbNavisworksModel");
        hdxf_test::assert_eq(model_class[3].second, "AcBIMUnderlayDbx");
        hdxf_test::assert_eq(def_class[2].second, "AcDbNavisworksModelDef");
    }
}

// Read back: the transform and the unit factor stay two fields, the two 290 of the definition stay
// apart, the file is kept byte for byte and an edit changes only what was edited
TEST(reference_2018_coordination_model_round_trip) {
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg = model_drawing();
        hdxf::Object* def = nullptr;
        for (auto& o : dwg.objects()) if (o.type == hdxf::ObjectType::NavisworksModelDef) def = &o;
        hdxf_test::assert_true(def != nullptr, "the definition is in objects()");
        def->as_navisworks_model_def().load = true;
        def->as_navisworks_model_def().host_visible = false;
        def->as_navisworks_model_def().min = {-1, -2, -3};
        def->as_navisworks_model_def().max = {4, 5, 6};
        std::vector<uint8_t> data = save(dwg, format);

        hdxf::Drawing back = load(data);
        hdxf_test::assert_eq(static_cast<int>(back.entities().size()), 1);
        const hdxf::Entity& e = back.entities()[0];
        hdxf_test::assert_true(e.type == hdxf::EntityType::CoordinationModel, "typed");
        const hdxf::CoordinationModelData& m = e.as_coordination_model();
        for (size_t i = 0; i < 16; ++i) hdxf_test::assert_eq(m.matrix[i], kMatrix[i], "matrix value");
        hdxf_test::assert_eq(m.unit_factor, 25.4, "unit factor");
        const hdxf::Object* d = back.find_object(m.definition_handle);
        hdxf_test::assert_true(d != nullptr && d->type == hdxf::ObjectType::NavisworksModelDef, "definition found");
        const hdxf::NavisworksModelDefData& n = d->as_navisworks_model_def();
        hdxf_test::assert_eq(n.path, "C:\\models\\plant.nwd");
        hdxf_test::assert_true(n.load, "load state");
        hdxf_test::assert_false(n.host_visible, "host visibility is the second 290");
        hdxf_test::assert_eq(n.min.y, -2.0);
        hdxf_test::assert_eq(n.max.z, 6.0);

        // Unchanged: the same bytes
        hdxf_test::assert_true(back.save_to_memory(format) == data, "unchanged is byte for byte");

        // An edit of the transform: only the transform changes
        back.entities()[0].as_coordination_model().matrix[3] = 11.0;
        std::vector<uint8_t> edited = save(back, format);
        hdxf::Drawing again = load(edited);
        hdxf_test::assert_eq(again.entities()[0].as_coordination_model().matrix[3], 11.0, "edited");
        hdxf_test::assert_eq(again.entities()[0].as_coordination_model().unit_factor, 25.4, "factor kept");
        hdxf_test::assert_eq(again.entities()[0].as_coordination_model().matrix[7], 20.0, "the rest kept");
        std::vector<Record> a = records_of(data), b = records_of(edited);
        hdxf_test::assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()), "same records");
        int different = 0;
        std::string which;
        for (size_t i = 0; i < a.size(); ++i) {
            // (the HEADER changes with the extents and the update time)
            if (a[i][0].second != "SECTION" && a[i] != b[i]) { ++different; which += " " + a[i][0].second + "#" + std::to_string(i); }
        }
        hdxf_test::assert_eq(different, 1, "one record differs:" + which);
    }
}

// The reference's name "Coordination Model" and "AcDbNavisworksModelDef" are read as the types
TEST(reference_2018_accepts_the_references_names) {
    hdxf::Drawing dwg = model_drawing();
    std::vector<uint8_t> data = save(dwg);
    std::string text(data.begin(), data.end());
    auto replace_all = [&](const std::string& from, const std::string& to) {
        for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
            text.replace(at, from.size(), to);
        }
    };
    replace_all("COORDINATION_MODEL", "Coordination Model");
    replace_all("NAVISWORKSMODELDEF", "AcDbNavisworksModelDef");
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(reinterpret_cast<const uint8_t*>(text.data()), text.size()),
                           back.last_error());
    hdxf_test::assert_true(back.entities().size() == 1 && back.entities()[0].type == hdxf::EntityType::CoordinationModel,
                           "the entity is typed");
    bool def = false;
    for (const auto& o : back.objects()) def = def || o.type == hdxf::ObjectType::NavisworksModelDef;
    hdxf_test::assert_true(def, "the definition is typed");
    // and written back under the names it had
    std::vector<uint8_t> again = save(back);
    hdxf_test::assert_true(again == std::vector<uint8_t>(text.begin(), text.end()), "unchanged: as it was");
}

// The model's extents: the definition's eight corners through the transform; the unit factor is not part of it
TEST(reference_2018_coordination_model_extents) {
    hdxf::Drawing dwg = model_drawing();
    hdxf::Point3 min, max;
    hdxf_test::assert_true(dwg.entity_extents(dwg.entities()[0], min, max), "extents");
    // (x, y) -> (-y + 10, x + 20): x in [7, 10], y in [20, 22], z in [30, 34]
    hdxf_test::assert_eq(min.x, 7.0);
    hdxf_test::assert_eq(max.x, 10.0);
    hdxf_test::assert_eq(min.y, 20.0);
    hdxf_test::assert_eq(max.y, 22.0);
    hdxf_test::assert_eq(min.z, 30.0);
    hdxf_test::assert_eq(max.z, 34.0);
}

// A removed definition leaves no dangling reference (340 is 0), in a new drawing and in a read one
TEST(reference_2018_removed_definition) {
    hdxf::Drawing dwg = model_drawing();
    std::string def_handle = dwg.entities()[0].as_coordination_model().definition_handle;
    auto remove_def = [&](hdxf::Drawing& d) {
        auto& objs = d.objects();
        for (auto& o : objs) {
            if (o.type != hdxf::ObjectType::Dictionary) continue;
            auto& entries = o.as_dictionary().entries;
            entries.erase(std::remove_if(entries.begin(), entries.end(),
                                         [&](const hdxf::DictionaryEntry& e) { return e.handle == def_handle; }),
                          entries.end());
        }
        objs.erase(std::remove_if(objs.begin(), objs.end(), [&](const hdxf::Object& o) { return o.handle == def_handle; }),
                   objs.end());
    };
    hdxf::Drawing fresh = model_drawing();
    def_handle = fresh.entities()[0].as_coordination_model().definition_handle;
    remove_def(fresh);
    std::vector<Record> recs = records_of(save(fresh));
    Record body = groups_in(find_type(recs, "COORDINATION_MODEL"), "AcDbNavisworksModel");
    hdxf_test::assert_eq(body[1].second, "0", "new drawing: the pointer is none");

    hdxf::Drawing saved = model_drawing();
    std::vector<uint8_t> data = save(saved);
    hdxf::Drawing back = load(data);
    def_handle = back.entities()[0].as_coordination_model().definition_handle;
    remove_def(back);
    recs = records_of(save(back));
    body = groups_in(find_type(recs, "COORDINATION_MODEL"), "AcDbNavisworksModel");
    hdxf_test::assert_eq(body[1].second, "0", "read drawing: the pointer is none");
    hdxf_test::assert_true(find_type(recs, "NAVISWORKSMODELDEF").empty(), "the definition is gone");
}

// Before 2013 there is no supported form: an explicit error; 2013 writes and reads it
TEST(reference_2018_older_versions) {
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R12, hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004,
                               hdxf::DxfVersion::R2007, hdxf::DxfVersion::R2010}) {
        hdxf::Drawing dwg = model_drawing(v);
        hdxf_test::assert_true(dwg.save_to_memory(hdxf::DxfFormat::Ascii).empty(), "refused");
        hdxf_test::assert_true(dwg.last_error().find("requires DXF 2013 or later") != std::string::npos,
                               "error: " + dwg.last_error());
    }
    // The RAPIDRT objects are in AutoCAD's 2000 … 2010 files too (AutoCAD 2025 writes them in every
    // version): only R12 refuses them
    hdxf::Drawing r12_settings(hdxf::DxfVersion::R12);
    r12_settings.add_rapidrt_render_settings(preset());
    hdxf_test::assert_true(r12_settings.save_to_memory(hdxf::DxfFormat::Ascii).empty(), "preset refused in R12");
    hdxf_test::assert_true(r12_settings.last_error().find("requires DXF 2013 or later") != std::string::npos,
                           "error: " + r12_settings.last_error());
    hdxf::Drawing r12_environment(hdxf::DxfVersion::R12);
    r12_environment.set_rapidrt_render_environment(hdxf::RapidRtRenderEnvironmentData{});
    hdxf_test::assert_true(r12_environment.save_to_memory(hdxf::DxfFormat::Ascii).empty(), "environment refused in R12");
    hdxf::Drawing dwg = model_drawing(hdxf::DxfVersion::R2013);
    hdxf::Drawing back = load(save(dwg));
    hdxf_test::assert_true(back.entities()[0].type == hdxf::EntityType::CoordinationModel, "2013");
}

// The render preset: the repeated 1, 90, 290, 70 and 40 are by position; the predefined flag last
TEST(reference_2018_rapidrt_settings) {
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2018);
        dwg.add_rapidrt_render_settings(preset());
        std::vector<uint8_t> data = save(dwg, format);
        if (format == hdxf::DxfFormat::Ascii) dxf_structure::check_structure(data, "rapidrt settings");
        std::vector<Record> recs = records_of(data);
        Record r = find_type(recs, "RAPIDRTRENDERSETTINGS");
        hdxf_test::assert_false(r.empty(), "written");
        Record base = groups_in(r, "AcDbRenderSettings");
        hdxf_test::assert_true(codes_of(base) == std::vector<int>({90, 1, 290, 290, 290, 290, 1, 1, 90}), "base subclass");
        Record rapid = groups_in(r, "AcDbRapidRTRenderSettings");
        hdxf_test::assert_true(codes_of(rapid) == std::vector<int>({90, 70, 90, 90, 70, 70, 40, 40, 290}),
                               "the predefined flag is last");
        hdxf_test::assert_eq(rapid[2].second, "7", "levels");
        hdxf_test::assert_eq(rapid[3].second, "33", "duration");
        hdxf_test::assert_eq(rapid[1].second, "0", "by time");
        hdxf_test::assert_eq(rapid[4].second, "2", "high accuracy");
        hdxf_test::assert_eq(rapid[5].second, "4", "Lanczos");

        hdxf::Drawing back = load(data);
        const hdxf::Object* o = nullptr;
        for (const auto& x : back.objects()) if (x.type == hdxf::ObjectType::RapidRtRenderSettings) o = &x;
        hdxf_test::assert_true(o != nullptr, "typed");
        const hdxf::RapidRtRenderSettingsData& d = o->as_rapidrt_settings();
        hdxf::RapidRtRenderSettingsData p = preset();
        hdxf_test::assert_eq(d.name, p.name);
        hdxf_test::assert_true(d.materials && d.texture_sampling && !d.back_faces && d.shadows, "flags by position");
        hdxf_test::assert_eq(d.preview_file, p.preview_file);
        hdxf_test::assert_eq(d.description, p.description);
        hdxf_test::assert_eq(d.display_index, 4);
        hdxf_test::assert_eq(d.levels, 7);
        hdxf_test::assert_eq(d.duration, 33);
        hdxf_test::assert_true(d.duration_type == hdxf::RenderDurationType::ByTime, "duration type");
        hdxf_test::assert_true(d.accuracy == hdxf::RenderAccuracy::High, "accuracy");
        hdxf_test::assert_true(d.filter == hdxf::SamplingFilter::Lanczos, "filter");
        hdxf_test::assert_eq(d.filter_width, 2.5);
        hdxf_test::assert_eq(d.filter_height, 4.5);
        hdxf_test::assert_true(d.predefined, "predefined");

        // Under its name in ACAD_RENDER_RAPIDRT_SETTINGS; unchanged is byte for byte; an edit is one record
        hdxf_test::assert_true(back.save_to_memory(format) == data, "unchanged is byte for byte");
        for (auto& x : back.objects()) {
            if (x.type == hdxf::ObjectType::RapidRtRenderSettings) x.as_rapidrt_settings().filter_width = 5.0;
        }
        std::vector<uint8_t> edited = save(back, format);
        std::vector<Record> a = records_of(data), b = records_of(edited);
        hdxf_test::assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()));
        int different = 0;
        std::string which;
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i][0].second != "SECTION" && a[i] != b[i]) { ++different; which += " " + a[i][0].second + "#" + std::to_string(i); }
        }
        hdxf_test::assert_eq(different, 1, "one record differs:" + which);
        Record er = groups_in(find_type(b, "RAPIDRTRENDERSETTINGS"), "AcDbRapidRTRenderSettings");
        hdxf_test::assert_eq(std::atof(er[6].second.c_str()), 5.0, "the width");
        hdxf_test::assert_eq(std::atof(er[7].second.c_str()), 4.5, "the height is kept");
    }
}

// The reference puts the predefined flag after the display index (AutoCAD's own file last): both are read
TEST(reference_2018_rapidrt_reference_order) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2018);
    dwg.add_rapidrt_render_settings(preset());
    std::vector<uint8_t> data = save(dwg);
    std::vector<std::string> lines;
    {
        std::string text(data.begin(), data.end());
        size_t from = 0;
        while (from < text.size()) {
            size_t nl = text.find('\n', from);
            if (nl == std::string::npos) nl = text.size() - 1;
            lines.push_back(text.substr(from, nl + 1 - from));
            from = nl + 1;
        }
    }
    auto trim = [](std::string s) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        size_t i = 0;
        while (i < s.size() && s[i] == ' ') ++i;
        return s.substr(i);
    };
    size_t rapid = 0;
    for (size_t i = 0; i + 1 < lines.size(); ++i) {
        if (trim(lines[i]) == "100" && trim(lines[i + 1]) == "AcDbRapidRTRenderSettings") rapid = i;
    }
    hdxf_test::assert_true(rapid > 0, "the subclass is there");
    size_t end = rapid;
    // the record's end: a code 0 line followed by a name
    while (!(trim(lines[end]) == "0" && end > rapid && !trim(lines[end + 1]).empty() &&
             std::isalpha(static_cast<unsigned char>(trim(lines[end + 1])[0])))) {
        ++end;
    }
    // the last group pair of the record (290, 1) moves to just before the second subclass
    std::vector<std::string> moved = {lines[end - 2], lines[end - 1]};
    hdxf_test::assert_eq(trim(moved[0]), "290");
    lines.erase(lines.begin() + static_cast<long>(end) - 2, lines.begin() + static_cast<long>(end));
    lines.insert(lines.begin() + static_cast<long>(rapid), moved.begin(), moved.end());
    std::string changed;
    for (const auto& l : lines) changed += l;
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(reinterpret_cast<const uint8_t*>(changed.data()), changed.size()),
                           back.last_error());
    const hdxf::Object* o = nullptr;
    for (const auto& x : back.objects()) if (x.type == hdxf::ObjectType::RapidRtRenderSettings) o = &x;
    hdxf_test::assert_true(o != nullptr, "typed");
    hdxf_test::assert_true(o->as_rapidrt_settings().predefined, "predefined after the display index");
    hdxf_test::assert_eq(o->as_rapidrt_settings().display_index, 4, "display index");
    hdxf_test::assert_eq(o->as_rapidrt_settings().levels, 7, "the repeated 90 of the second subclass");
}

// The IBL environment: ACAD_BACKGROUND of the root dictionary; the background it replaces is its custom background
TEST(reference_2018_rapidrt_environment) {
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2018);
        hdxf::RapidRtRenderEnvironmentData d;
        d.enabled = true;
        d.ibl_file = "C:\\maps\\sky.hdr";
        d.rotation = -45.5;
        d.ibl_as_background = true;
        d.background_handle = "";
        dwg.set_rapidrt_render_environment(d);
        std::vector<uint8_t> data = save(dwg, format);
        if (format == hdxf::DxfFormat::Ascii) dxf_structure::check_structure(data, "rapidrt environment");
        std::vector<Record> recs = records_of(data);
        Record r = find_type(recs, "RAPIDRTRENDERENVIRONMENT");
        hdxf_test::assert_false(r.empty(), "written");
        Record body = groups_in(r, "AcDbIBLBackground");
        hdxf_test::assert_true(codes_of(body) == std::vector<int>({90, 290, 1, 40, 290, 340}), "order");
        hdxf_test::assert_eq(body[1].second, "1");
        hdxf_test::assert_eq(body[4].second, "1");
        hdxf_test::assert_eq(std::atof(body[3].second.c_str()), -45.5);
        std::string handle = values_of(r, 5)[0];
        bool entered = false;
        for (const auto& rec : recs) {
            if (rec[0].second != "DICTIONARY") continue;
            for (size_t i = 0; i + 1 < rec.size(); ++i) {
                if (rec[i].first == 3 && rec[i].second == "ACAD_BACKGROUND" && rec[i + 1].second == handle) entered = true;
            }
        }
        hdxf_test::assert_true(entered, "the root dictionary's ACAD_BACKGROUND is the environment");
        hdxf_test::assert_true(!find_type(recs, "RAPIDRTRENDERENVIRONMENT", true).empty(), "its class");
        hdxf::Drawing back = load(data);
        const hdxf::Object* o = nullptr;
        for (const auto& x : back.objects()) if (x.type == hdxf::ObjectType::RapidRtRenderEnvironment) o = &x;
        hdxf_test::assert_true(o != nullptr, "typed");
        hdxf_test::assert_true(o->as_rapidrt_environment().enabled && o->as_rapidrt_environment().ibl_as_background, "flags");
        hdxf_test::assert_eq(o->as_rapidrt_environment().ibl_file, "C:\\maps\\sky.hdr");
        hdxf_test::assert_eq(o->as_rapidrt_environment().rotation, -45.5);
        hdxf_test::assert_true(back.save_to_memory(format) == data, "unchanged is byte for byte");
    }
}

// A background that was there becomes the custom background (340)
TEST(reference_2018_rapidrt_environment_keeps_the_background) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2018);
    hdxf::RapidRtRenderEnvironmentData first;
    first.ibl_file = "a.hdr";
    hdxf::Object& one = dwg.set_rapidrt_render_environment(first);
    std::string one_handle = one.handle;
    // another kind of background in its place, as a plain entry
    hdxf::RapidRtRenderEnvironmentData second;
    second.ibl_file = "b.hdr";
    hdxf::Object& two = dwg.set_rapidrt_render_environment(second);
    hdxf_test::assert_eq(two.handle, one_handle, "an environment is replaced in place");
    hdxf_test::assert_eq(two.as_rapidrt_environment().ibl_file, "b.hdr");
}
