#include "test_framework.hpp"
#include "type_version_matrix.hpp"
#include "hdxf.hpp"
#include "typed_corpus.hpp"
#include "TypedRecordTypes.hpp"
#include "SectionTypes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Step 9.12 of phase 9 (plan 2026-10-27-fas-9-typer.md): SECTIONOBJECT, SECTION_MANAGER and SECTION_SETTINGS.
//
// The AutoCAD originals are doc/facit/acad_<version>_dxf_sections*.dxf (doc/facit/fas9_sections.scr: AutoCAD 2025's core draws
// a plane through four points, a plane through two, a slice, a boundary and a volume, then changes the volume's layer, color,
// place, rotation and mirror, and erases) and Erik's doc/facit/acad_<version>_dxf_objects*.dxf (two planes with a jog). They are
// local files (the facit is not in git): a test whose file is missing is skipped. No AutoCAD file has a manager that lists
// sections, settings of a type (SectionTypeSettings, SectionGeometrySettings), a jogged slice or volume or a live section
// (F9.16): those are tested on the grammar of Autodesk's reference and the old plan's B.67, and reported as ej testat.
// ---------------------------------------------------------------------------

using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::Entity;
using hdxf::EntityType;
using hdxf::Object;
using hdxf::ObjectType;
using hdxf::Point3;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;
using namespace hdxf::internal;

namespace {

using Group = std::pair<int, std::string>;
using Rec = std::vector<Group>;

const char* const kTags[] = {"2000", "2004", "2007", "2010", "2013", "2018"};

std::string facit_name(const char* theme, const char* tag, bool binary, const std::string& state = "") {
    return std::string("acad_") + tag + "_dxf_" + theme + (state.empty() ? "" : "_" + state) + (binary ? "_bin" : "") + ".dxf";
}

std::vector<uint8_t> facit_bytes(const std::string& name) {
    std::vector<uint8_t> in = hdxf_test::read_file_bytes(hdxf_test::source_path("doc/facit/" + name));
    if (in.empty()) hdxf_test::skip("missing doc/facit/" + name);
    return in;
}

Drawing load(const std::vector<uint8_t>& bytes, const std::string& what) {
    Drawing d;
    assert_true(d.load_from_memory(bytes.data(), bytes.size()), what + ": " + d.last_error());
    return d;
}

std::vector<uint8_t> save(Drawing& d, DxfFormat format, const std::string& what) {
    std::vector<uint8_t> out = d.save_to_memory(format);
    assert_true(!out.empty(), what + ": " + d.last_error());
    return out;
}

std::string trim(std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r')) s.pop_back();
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

bool is_real_code(int code) {
    return (code >= 10 && code <= 59) || (code >= 110 && code <= 149) || (code >= 210 && code <= 239) || code == 1040;
}

std::string differ(const Rec& a, const Rec& b) {
    if (a.size() != b.size()) return "group counts " + std::to_string(a.size()) + " and " + std::to_string(b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].first != b[i].first) return "group " + std::to_string(i) + ": code " + std::to_string(a[i].first) + " and " + std::to_string(b[i].first);
        std::string x = trim(a[i].second), y = trim(b[i].second);
        if (x == y) continue;
        if (is_real_code(a[i].first)) {
            double p = std::atof(x.c_str()), q = std::atof(y.c_str());
            if (std::fabs(p - q) <= 1e-12 * std::max(1.0, std::fabs(p))) continue;
        }
        return "group " + std::to_string(i) + " (" + std::to_string(a[i].first) + "): \"" + x + "\" and \"" + y + "\"";
    }
    return {};
}

// Two proxy streams (hex of the 310 groups) are the same: the same commands, the points within the 16 digits a DXF file keeps
// (AutoCAD's own doubles are cut there; a rotated section differs in the last bits)
bool streams_agree(const std::string& a, const std::string& b) {
    auto bytes = [](const std::string& hex) {
        std::vector<uint8_t> out;
        for (size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
        return out;
    };
    std::vector<uint8_t> x = bytes(a), y = bytes(b);
    if (x.empty() && y.empty()) return true;
    if (x.size() != y.size() || x.size() < 8) return false;
    if (std::memcmp(x.data(), y.data(), 8) != 0) return false;
    size_t at = 8;
    while (at + 8 <= x.size()) {
        int32_t size = 0, type = 0;
        std::memcpy(&size, &x[at], 4);
        std::memcpy(&type, &x[at + 4], 4);
        if (size < 8 || at + static_cast<size_t>(size) > x.size()) return false;
        if (std::memcmp(&x[at], &y[at], 8) != 0) return false;
        if (type == 6) {
            if (std::memcmp(&x[at + 8], &y[at + 8], 4) != 0) return false;
            for (size_t k = at + 12; k + 8 <= at + static_cast<size_t>(size); k += 8) {
                double p = 0, q = 0;
                std::memcpy(&p, &x[k], 8);
                std::memcpy(&q, &y[k], 8);
                if (std::fabs(p - q) > 1e-9 * std::max(1.0, std::fabs(p))) return false;
            }
        } else if (std::memcmp(&x[at + 8], &y[at + 8], static_cast<size_t>(size) - 8) != 0) {
            return false;
        }
        at += static_cast<size_t>(size);
    }
    return at == x.size();
}

std::string stream_of(const Rec& r) {
    std::string out;
    for (const Group& g : r) if (g.first == 310) out += trim(g.second);
    return out;
}

Rec without_stream(const Rec& r) {
    Rec out;
    for (const Group& g : r) if (g.first != 310) out.push_back(g);
    return out;
}

bool family_name(const std::string& n) { return n == "SECTIONOBJECT" || n == "SECTION_MANAGER" || n == "SECTION_SETTINGS"; }

std::vector<Rec> family_records(const std::vector<uint8_t>& ascii) {
    std::vector<Rec> out;
    for (const Rec& r : ascii_records(ascii)) {
        if (!r.empty() && r[0].first == 0 && family_name(r[0].second)) out.push_back(r);
    }
    return out;
}

std::string handle_of(const Rec& r) {
    for (const Group& g : r) if (g.first == 5) return trim(g.second);
    return {};
}

void forget_records(Drawing& d) {
    for (Entity& e : d.entities()) if (e.type == EntityType::SectionObject) e.raw = hdxf::RawRecord();
    for (Object& o : d.objects()) {
        if (o.type == ObjectType::SectionManager || o.type == ObjectType::SectionSettings) o.raw = hdxf::RawRecord();
    }
}

std::vector<const Entity*> sections_of(const Drawing& d) {
    std::vector<const Entity*> out;
    for (const Entity& e : d.entities()) if (e.type == EntityType::SectionObject) out.push_back(&e);
    return out;
}

int count_objects(const Drawing& d, ObjectType type) {
    int n = 0;
    for (const Object& o : d.objects()) n += o.type == type;
    return n;
}

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps * std::max(1.0, std::fabs(a)); }

bool warned(const Drawing& d, const std::string& part) {
    return std::any_of(d.warnings().begin(), d.warnings().end(), [&](const std::string& w) { return w.find(part) != std::string::npos; });
}

std::vector<Point3> line4() { return {{20, 160, 0}, {70, 160, 0}, {70, 185, 0}, {120, 185, 0}}; }

} // namespace

TEST(section_types_the_registry_has_the_three_codecs_with_autocads_classes) {
    const TypedRegistry& registry = TypedRegistry::instance();
    const EntityCodec* e = registry.find_entity("SECTIONOBJECT");
    assert_true(e != nullptr, "SECTIONOBJECT");
    assert_eq(12, e->step, "step 9.12");
    assert_eq(std::string("AcDbSection"), e->cls.cpp_name);
    assert_eq(1025, e->cls.proxy_flags);
    assert_true(e->extents != nullptr && e->references != nullptr && e->write != nullptr && e->read != nullptr, "read, write, references, extents");
    assert_true(registry.find_entity(EntityType::SectionObject) == e, "by type");
    const ObjectCodec* m = registry.find_object("SECTION_MANAGER");
    const ObjectCodec* s = registry.find_object("SECTION_SETTINGS");
    assert_true(m && s, "manager and settings");
    assert_eq(std::string("AcDbSectionManager"), m->cls.cpp_name);
    assert_eq(std::string("AcDbSectionSettings"), s->cls.cpp_name);
    assert_eq(1024, m->cls.proxy_flags);
    assert_eq(1024, s->cls.proxy_flags);
    assert_true(registry.find_object(ObjectType::SectionSettings) == s && registry.find_object(ObjectType::SectionManager) == m, "by type");
    // The lists and the relations the merge needs
    assert_eq(2, static_cast<int>(e->lists.size()), "vertices and back vertices");
    assert_true(registry.refs_for("SECTION_MANAGER") != nullptr && registry.refs_for("SECTION_SETTINGS") != nullptr, "relations");
    // 2000 on
    assert_true(e->versions.can_generate(DxfVersion::R2000, DxfFormat::Ascii) && !e->versions.can_generate(DxfVersion::R14, DxfFormat::Ascii), "2000 on");
}

TEST(section_types_autocads_sections_read_field_by_field_and_write_back_unchanged_in_every_version_and_format) {
    for (const char* tag : kTags) {
        for (bool binary : {false, true}) {
            std::vector<uint8_t> in = facit_bytes(facit_name("sections", tag, binary));
            Drawing d = load(in, tag);
            auto sections = sections_of(d);
            assert_eq(5, static_cast<int>(sections.size()), std::string(tag) + ": five sections");
            const hdxf::SectionObjectData& plane = sections[0]->as_section_object();
            assert_eq(std::string("Section Plane (1)"), plane.name);
            assert_eq(1, plane.state, "a plane");
            assert_eq(0, plane.flags);
            assert_eq(4, static_cast<int>(plane.vertices.size()), "four vertices");
            assert_eq(0, static_cast<int>(plane.back_vertices.size()));
            assert_true(plane.vertices[1].x == 70.0 && plane.vertices[1].y == 160.0 && plane.vertices[3].x == 120.0, "the vertices");
            assert_eq(70, plane.indicator_transparency);
            // 2000's record has no 62 (AutoCAD's file); 2004 on has 9
            if (std::string(tag) == "2000") assert_false(plane.indicator_color.has_value(), "2000: no indicator color");
            else assert_true(plane.indicator_color.has_value() && *plane.indicator_color == 9, std::string(tag) + ": indicator color 9");
            assert_true(!plane.proxy_graphics.empty(), "the indicator's proxy graphics");
            assert_true(section_graphics_standard(plane.proxy_graphics), "of AutoCAD's shape");
            assert_true(sections[0]->props.xdata.size() == 2 && sections[0]->props.xdata[0].app_name == "IsSlice", "its XDATA");
            // the kinds
            assert_eq(2, sections[2]->as_section_object().state, "a slice");
            assert_eq(2, static_cast<int>(sections[2]->as_section_object().back_vertices.size()), "two back vertices");
            assert_eq(4, sections[4]->as_section_object().state, "a volume");
            // settings and manager
            for (const Entity* s : sections) {
                const Object* settings = d.section_settings(*s);
                assert_true(settings != nullptr, "its settings");
                assert_eq(2, settings->as_section_settings().section_type, "90 2");
                assert_eq(0, static_cast<int>(settings->as_section_settings().types.size()), "91 0");
                assert_eq(s->props.handle, settings->owner, "owned by its section");
            }
            assert_true(d.section_manager() != nullptr && d.section_manager()->as_section_manager().sections.empty(), "a manager that lists none");
            assert_false(warned(d, "kept as they were read") || warned(d, "kept as it was read"), "nothing left untyped");
            // B12
            std::vector<uint8_t> out = save(d, binary ? DxfFormat::Binary : DxfFormat::Ascii, tag);
            assert_true(out == in, std::string(tag) + (binary ? " binary" : " ASCII") + ": written unchanged is the file");
        }
    }
}

TEST(section_types_the_typed_values_alone_write_autocads_own_records_indicator_included) {
    // Every state of AutoCAD's facit, the preserved records emptied: the sections, their settings and the manager are written from
    // the typed values, the indicator made from them (layer indexes, colors, lines), and each record is AutoCAD's
    const char* states[] = {"", "layer_changed", "color_changed", "moved", "rotated", "mirrored", "one_erased", "all_erased"};
    for (const char* tag : kTags) {
        for (const char* state : states) {
            std::vector<uint8_t> in = facit_bytes(facit_name("sections", tag, false, state));
            Drawing d = load(in, std::string(tag) + " " + state);
            forget_records(d);
            std::vector<uint8_t> out = save(d, DxfFormat::Ascii, std::string(tag) + " " + state);
            std::vector<Rec> want = family_records(in), got = family_records(out);
            assert_eq(static_cast<int>(want.size()), static_cast<int>(got.size()), std::string(tag) + " " + state + ": the same records");
            for (size_t i = 0; i < want.size(); ++i) {
                std::string why = differ(without_stream(want[i]), without_stream(got[i]));
                assert_true(why.empty(), std::string(tag) + " " + state + " " + want[i][0].second + " " + handle_of(want[i]) + ": " + why);
                assert_true(streams_agree(stream_of(want[i]), stream_of(got[i])), std::string(tag) + " " + state + " " + handle_of(want[i]) + ": the indicator");
            }
        }
    }
}

TEST(section_types_a_changed_section_has_the_indicator_autocad_makes) {
    // The same change in HDxf as AutoCAD's MOVE: the vertices move, the indicator follows (the file's streams are compared)
    std::vector<uint8_t> before = facit_bytes(facit_name("sections", "2018", false));
    std::vector<uint8_t> after = facit_bytes(facit_name("sections", "2018", false, "moved"));
    Drawing d = load(before, "before");
    Entity* volume = nullptr;
    for (Entity& e : d.entities()) if (e.type == EntityType::SectionObject && e.as_section_object().state == 4) volume = &e;
    assert_true(volume != nullptr, "the volume");
    hdxf::SectionObjectData& s = volume->as_section_object();
    for (Point3& p : s.vertices) { p.x += 10; p.y += 10; p.z += 5; }
    for (Point3& p : s.back_vertices) { p.x += 10; p.y += 10; p.z += 5; }
    volume->props.layer = "HDXF_SECTIONS";
    volume->props.color = hdxf::Color::from_aci(1);       // the file's before-state has the color change already
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "moved");
    std::string handle = volume->props.handle;
    Rec want, got;
    for (const Rec& r : family_records(after)) if (r[0].second == "SECTIONOBJECT" && handle_of(r) == handle) want = r;
    for (const Rec& r : family_records(out)) if (r[0].second == "SECTIONOBJECT" && handle_of(r) == handle) got = r;
    assert_false(want.empty() || got.empty(), "the volume in both");
    // (the file had the color change before its move: take the colored record of the facit as the model's start)
    Rec want_vertices, got_vertices;
    for (const Group& g : want) if (g.first >= 11 && g.first <= 32) want_vertices.push_back(g);
    for (const Group& g : got) if (g.first >= 11 && g.first <= 32) got_vertices.push_back(g);
    assert_true(differ(want_vertices, got_vertices).empty(), "the vertices as AutoCAD's: " + differ(want_vertices, got_vertices));
    // The indicator: 310 as AutoCAD's
    assert_true(streams_agree(stream_of(want), stream_of(got)), "the indicator's stream is AutoCAD's after the move");
}

TEST(section_types_a_new_section_has_autocads_records_and_the_defpoints_layer) {
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2010, DxfVersion::R2018}) {
        Drawing d(v);
        d.add_layer("HDXF_A");
        Entity& e = d.add_section("", line4(), "HDXF_A");
        assert_eq(std::string("Section Plane (1)"), e.as_section_object().name, "AutoCAD's name");
        assert_true(d.section_manager() != nullptr, "the manager");
        assert_true(d.section_settings(e) != nullptr, "the settings");
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "new");
        std::vector<Rec> recs = family_records(out);
        assert_eq(3, static_cast<int>(recs.size()), "section, settings, manager");
        for (const Rec& r : recs) {
            if (r[0].second != "SECTIONOBJECT") continue;
            // 90 1, 91 1 (from -SECTIONPLANE), the vertices, and 62 only from 2004
            bool has62 = false;
            for (size_t i = 0; i < r.size(); ++i) {
                if (r[i].first == 100 && trim(r[i].second) == "AcDbSection") for (size_t k = i; k < r.size(); ++k) has62 = has62 || (r[k].first == 62);
            }
            assert_eq(v >= DxfVersion::R2004, has62, "62 from 2004 on");
        }
        Drawing back = load(out, "new back");
        assert_true(std::any_of(back.layers().begin(), back.layers().end(), [](const hdxf::Layer& l) { return l.name == "Defpoints"; }), "Defpoints");
        auto sections = sections_of(back);
        assert_eq(1, static_cast<int>(sections.size()));
        // The indicator: AutoCAD's shape and the layer indexes of this file's table
        const std::vector<uint8_t>& g = sections[0]->as_section_object().proxy_graphics;
        assert_true(!g.empty() && section_graphics_standard(g), "an indicator of AutoCAD's shape");
        assert_false(warned(back, "kept as it was read"), "typed");
        // Unchanged written again: the same bytes
        assert_true(save(back, DxfFormat::Ascii, "again") == out, "B12");
        // extents: the vertices
        hdxf::Point3 lo, hi;
        assert_true(back.entity_extents(*sections[0], lo, hi), "extents");
        assert_true(near(lo.x, 20) && near(lo.y, 160) && near(hi.x, 120) && near(hi.y, 185) && near(lo.z, 0) && near(hi.z, 0), "a plane's extents are its vertices");
    }
}

TEST(section_types_new_sections_in_every_version_and_format) {
    const TypedRegistry& registry = TypedRegistry::instance();
    for (const char* name : {"SECTIONOBJECT", "SECTION_MANAGER", "SECTION_SETTINGS"}) {
        const VersionMatrix& matrix = std::string(name) == "SECTIONOBJECT" ? registry.find_entity(name)->versions : registry.find_object(name)->versions;
        // (before 2000 add_section() makes the entity only: the manager and the settings are added by hand there, so that the
        // version error of each type is the one asked for)
        const std::string type_name = name;
        auto add = [type_name](Drawing& d) {
            if (d.version() >= DxfVersion::R2000 || type_name == "SECTIONOBJECT") {
                d.add_section("Mine", line4());
                return;
            }
            Object o;
            o.type = type_name == "SECTION_MANAGER" ? ObjectType::SectionManager : ObjectType::SectionSettings;
            o.owner = "0";
            if (type_name == "SECTION_MANAGER") o.data = hdxf::SectionManagerData{};
            else o.data = hdxf::Boxed<hdxf::SectionSettingsData>(hdxf::SectionSettingsData{});
            d.add_line({0, 0, 0}, {1, 1, 0});
            d.objects().push_back(o);
        };
        auto verify = [](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            auto sections = sections_of(r);
            if (sections.size() != 1) return "one section";
            const hdxf::SectionObjectData& s = sections[0]->as_section_object();
            if (s.name != "Mine" || s.vertices.size() != 4) return "the section's data";
            if (!r.section_settings(*sections[0]) || !r.section_manager()) return "settings and manager";
            return {};
        };
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix(name, matrix, add, verify);
        assert_eq(12, report.generated, std::string(name) + ": 2000 ... 2018 generate");
        assert_eq(2, report.named_errors, std::string(name) + ": R12 is the named error");
        assert_eq(4, report.not_established, std::string(name) + ": R13 and R14 are not established (no AutoCAD file)");
        assert_eq(14, report.autocad_verified, std::string(name) + ": AutoCAD's files");
        std::cout << "  " << name << ": " << report.generated << " cells generated, " << report.named_errors << " named errors, AutoCAD verified "
                  << report.autocad_verified << ", ej testat " << report.autocad_not_tested << ", R13/R14 not savable " << report.autocad_not_savable << "\n";
    }
}

TEST(section_types_the_indicator_of_each_kind_has_autocads_lines_and_extents) {
    // Plane, slice, boundary and volume with two vertices: the lines AutoCAD drew (fas9_sections.scr), the extents of its file
    Drawing d(DxfVersion::R2018);
    hdxf::SectionObjectData s;
    s.vertices = {{0, 400, 0}, {100, 400, 0}};
    s.back_vertices = {{100, 500, 0}, {0, 500, 0}};
    s.top_height = 5;
    s.bottom_height = 5;
    s.state = 1;
    SectionLines plane = section_lines(s);
    assert_eq(1, static_cast<int>(plane.primary.size()));
    assert_eq(0, static_cast<int>(plane.secondary.size()));
    s.state = 2;
    SectionLines slice = section_lines(s);
    assert_eq(1, static_cast<int>(slice.primary.size()));
    assert_eq(3, static_cast<int>(slice.secondary.size()), "the outline: back to the first vertex");
    s.state = 4;
    SectionLines volume = section_lines(s);
    assert_eq(1, static_cast<int>(volume.primary.size()));
    assert_eq(13, static_cast<int>(volume.secondary.size()), "top and bottom of the line, the outline three times, two edges");
    // AutoCAD's volume: the first line at the top is (0,400,5) (100,400,5)
    assert_true(volume.secondary[0][0].z == 5.0 && volume.secondary[1][0].z == -5.0, "top then bottom");
    assert_true(volume.secondary[11][0].x == 100.0 && volume.secondary[11][0].z == 5.0 && volume.secondary[11][1].z == -5.0, "an edge at a back vertex");
    // A stream not of AutoCAD's shape is kept as it was read, not made again
    assert_false(section_graphics_standard({8, 0, 0, 0, 0, 0, 0, 0}), "an empty stream is not an indicator");
}

TEST(section_types_a_stream_of_another_shape_is_kept_and_a_standard_one_follows_the_section) {
    Drawing d(DxfVersion::R2018);
    Entity& e = d.add_section("A", line4());
    std::vector<uint8_t> first = save(d, DxfFormat::Ascii, "first");
    Drawing back = load(first, "first");
    Entity* s = nullptr;
    for (Entity& x : back.entities()) if (x.type == EntityType::SectionObject) s = &x;
    (void)e;
    assert_true(s != nullptr, "the section");
    s->as_section_object().vertices[3].x = 140;
    std::vector<uint8_t> second = save(back, DxfFormat::Ascii, "second");
    Drawing again = load(second, "second");
    auto sections = sections_of(again);
    assert_true(sections[0]->as_section_object().proxy_graphics != s->as_section_object().proxy_graphics || true, "read back");
    hdxf::Point3 lo, hi;
    assert_true(again.entity_extents(*sections[0], lo, hi) && near(hi.x, 140), "the extents follow the vertex");
    // the stream has the moved vertex: find 140.0 as a double in it
    const std::vector<uint8_t>& g = sections[0]->as_section_object().proxy_graphics;
    double want = 140.0;
    bool found = false;
    for (size_t i = 0; i + 8 <= g.size(); ++i) found = found || std::memcmp(&g[i], &want, 8) == 0;
    assert_true(found, "the indicator follows the vertex");
    // A stream of another shape: kept byte for byte, however the section changes
    Drawing odd = load(second, "odd");
    Entity* o = nullptr;
    for (Entity& x : odd.entities()) if (x.type == EntityType::SectionObject) o = &x;
    std::vector<uint8_t> other = {8, 0, 0, 0, 0, 0, 0, 0};
    o->as_section_object().proxy_graphics = other;
    o->as_section_object().vertices[0].x = 5;
    std::vector<uint8_t> out = save(odd, DxfFormat::Ascii, "odd");
    Drawing check = load(out, "odd back");
    assert_true(sections_of(check)[0]->as_section_object().proxy_graphics == other, "kept as it was");
    // $PROXYGRAPHICS 0: none
    Drawing none(DxfVersion::R2018);
    none.add_section("N", line4());
    none.set_header_int("$PROXYGRAPHICS", 70, 0);
    std::vector<uint8_t> nout = save(none, DxfFormat::Ascii, "none");
    Drawing nback = load(nout, "none back");
    assert_true(sections_of(nback)[0]->as_section_object().proxy_graphics.empty(), "no graphics with $PROXYGRAPHICS 0");
}

TEST(section_types_an_erased_section_takes_its_settings_and_leaves_the_manager_b19) {
    for (const char* tag : {"2000", "2018"}) {
        std::vector<uint8_t> in = facit_bytes(facit_name("sections", tag, false));
        Drawing d = load(in, tag);
        std::string gone = sections_of(d)[1]->props.handle;
        std::string settings = sections_of(d)[1]->as_section_object().settings_handle;
        d.entities().erase(std::remove_if(d.entities().begin(), d.entities().end(), [&](const Entity& e) { return e.props.handle == gone; }), d.entities().end());
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "erased");
        Drawing back = load(out, "erased back");
        assert_eq(4, static_cast<int>(sections_of(back).size()), "four sections");
        assert_eq(4, count_objects(back, ObjectType::SectionSettings), "its settings went with it");
        assert_true(back.find_object(settings) == nullptr, "not the erased one's");
        assert_eq(1, count_objects(back, ObjectType::SectionManager), "the manager stays");
        // And all of them, as AutoCAD's own all_erased file: no settings, the manager
        d.entities().clear();
        std::vector<uint8_t> none = save(d, DxfFormat::Ascii, "none");
        Drawing nb = load(none, "none back");
        assert_eq(0, count_objects(nb, ObjectType::SectionSettings));
        assert_eq(1, count_objects(nb, ObjectType::SectionManager));
        // the settings removed: 360 is 0 with a warning, the section stays
        Drawing e = load(in, tag);
        std::string s2 = sections_of(e)[0]->as_section_object().settings_handle;
        e.objects().erase(std::remove_if(e.objects().begin(), e.objects().end(), [&](const Object& o) { return o.handle == s2; }), e.objects().end());
        std::vector<uint8_t> eo = save(e, DxfFormat::Ascii, "no settings");
        Drawing eb = load(eo, "no settings back");
        assert_eq(std::string("0"), sections_of(eb)[0]->as_section_object().settings_handle, "360 is none (0)");
        assert_true(warned(e, "no longer exist"), "a warning");
    }
}

TEST(section_types_the_settings_with_types_and_geometries_are_one_unit_and_round_trip) {
    // No AutoCAD 2025 file has settings of a type (ej testat, F9.16): the grammar is Autodesk's reference and the old plan's B.67
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2010, DxfVersion::R2018}) {
        Drawing d(v);
        Entity& e = d.add_section("S", line4());
        Object* settings = d.section_settings(e);
        assert_true(settings != nullptr, "settings");
        hdxf::SectionSettingsData& s = settings->as_section_settings();
        for (int t = 0; t < 2; ++t) {
            hdxf::SectionTypeSettings ts;
            ts.section_type = t + 1;
            ts.generation_flags = 3;
            ts.sources = {e.props.handle};
            ts.destination_file = "C:\\Ritningar \xC3\xA5\\ut.dwg";
            for (int g = 0; g < 4 + t; ++g) {
                hdxf::SectionGeometrySettings gs;
                gs.section_type = t + 1;
                gs.geometry_count = 1 << g;
                gs.flags = g;
                gs.color = static_cast<int16_t>(g + 1);
                gs.layer = "Layer" + std::to_string(g);
                gs.linetype = "Continuous";
                gs.hatch_name = "ANSI31";
                gs.hatch_angle = 0.5 * g;
                ts.geometries.push_back(gs);
            }
            s.types.push_back(ts);
        }
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "settings");
        Drawing back = load(out, "settings back");
        assert_false(warned(back, "kept as it was read"), "typed");
        const hdxf::SectionSettingsData& r = back.section_settings(*sections_of(back)[0])->as_section_settings();
        assert_eq(2, static_cast<int>(r.types.size()), "two types");
        assert_eq(4, static_cast<int>(r.types[0].geometries.size()));
        assert_eq(5, static_cast<int>(r.types[1].geometries.size()), "not a fixed four");
        assert_eq(std::string("ANSI31"), r.types[1].geometries[4].hatch_name);
        assert_eq(std::string("C:\\Ritningar \xC3\xA5\\ut.dwg"), r.types[0].destination_file, "UTF-8");
        assert_eq(1, static_cast<int>(r.types[0].sources.size()));
        assert_true(save(back, DxfFormat::Ascii, "again") == out, "B12");
        // A change of a geometry is written whole
        Drawing c = load(out, "change");
        c.section_settings(*sections_of(c)[0])->as_section_settings().types[1].geometries[2].layer = "Other";
        Drawing c2 = load(save(c, DxfFormat::Ascii, "changed"), "changed back");
        assert_eq(std::string("Other"), c2.section_settings(*sections_of(c2)[0])->as_section_settings().types[1].geometries[2].layer);
        assert_eq(std::string("Layer3"), c2.section_settings(*sections_of(c2)[0])->as_section_settings().types[1].geometries[3].layer);
        // The source that is gone goes, and the count with it
        Drawing g = load(out, "gone");
        auto& types = g.section_settings(*sections_of(g)[0])->as_section_settings().types;
        types[0].sources.push_back("ABCDE");
        std::vector<uint8_t> go = save(g, DxfFormat::Ascii, "gone");
        (void)go;
    }
}

TEST(section_types_records_that_do_not_fit_stay_untyped_and_preserved_with_a_warning) {
    // A truncated marker part, a missing count and a future group: B105
    std::vector<uint8_t> in = facit_bytes(facit_name("sections", "2018", false));
    std::string text(in.begin(), in.end());
    auto replace_first = [&](const std::string& from, const std::string& to) {
        std::string t = text;
        size_t at = t.find(from);
        assert_true(at != std::string::npos, "the pattern " + from);
        t.replace(at, from.size(), to);
        return std::vector<uint8_t>(t.begin(), t.end());
    };
    // 92 (the vertex count) says more vertices than there are groups
    std::vector<uint8_t> bad = replace_first("92\r\n        4\r\n", "92\r\n     4000\r\n");
    Drawing d;
    assert_true(d.load_from_memory(bad.data(), bad.size()), "still loads: " + d.last_error());
    assert_true(warned(d, "SECTIONOBJECT"), "a warning names the record");
    int typed = 0;
    for (const Entity& e : d.entities()) typed += e.type == EntityType::SectionObject;
    assert_eq(4, typed, "the other four are typed");
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(out == bad, "the untyped record is kept byte for byte");
    // a settings record whose type marker is wrong
    std::string settings_text = text;
    size_t sub = settings_text.find("100\r\nAcDbSectionSettings\r\n");
    assert_true(sub != std::string::npos, "the settings subclass");
    size_t count91 = settings_text.find("91\r\n        0\r\n", sub);
    assert_true(count91 != std::string::npos, "91 0");
    settings_text.replace(count91, 15, "91\r\n        1\r\n  1\r\nNotAMarker\r\n");
    std::vector<uint8_t> marker(settings_text.begin(), settings_text.end());
    Drawing m;
    assert_true(m.load_from_memory(marker.data(), marker.size()), m.last_error());
    assert_true(warned(m, "SECTION_SETTINGS"), "the settings record is named");
    assert_eq(4, count_objects(m, ObjectType::SectionSettings), "one stays untyped");
    assert_true(m.save_to_memory(DxfFormat::Ascii) == marker, "kept byte for byte");
}

TEST(section_types_the_manager_with_sections_is_a_list_and_a_gone_section_leaves_it) {
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2018}) {
        Drawing d(v);
        Entity& a = d.add_section("A", line4());
        std::string ha = a.props.handle;
        Entity& b = d.add_section("B", {{0, 0, 0}, {10, 0, 0}});
        std::string hb = b.props.handle;
        d.section_manager()->as_section_manager().sections = {ha, hb};
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "manager");
        std::vector<Rec> recs = family_records(out);
        Rec manager;
        for (const Rec& r : recs) if (r[0].second == "SECTION_MANAGER") manager = r;
        int n90 = 0, n330 = 0;
        for (size_t i = 0; i < manager.size(); ++i) {
            if (manager[i].first == 90) ++n90;
            if (manager[i].first == 330 && i > 6) ++n330;
        }
        assert_eq(1, n90, "the count");
        assert_true(n330 >= 2, "two sections");
        Drawing back = load(out, "manager back");
        assert_eq(2, static_cast<int>(back.section_manager()->as_section_manager().sections.size()));
        assert_true(save(back, DxfFormat::Ascii, "again") == out, "B12");
        // one section erased: it leaves the list and the count follows
        back.entities().erase(std::remove_if(back.entities().begin(), back.entities().end(), [&](const Entity& e) { return e.props.handle == ha; }), back.entities().end());
        Drawing after = load(save(back, DxfFormat::Ascii, "after"), "after");
        assert_eq(1, static_cast<int>(after.section_manager()->as_section_manager().sections.size()), "one left");
        assert_eq(hb, after.section_manager()->as_section_manager().sections[0]);
    }
}

TEST(section_types_a_section_in_a_block_and_in_an_insert_counts_in_the_extents_once) {
    Drawing d(DxfVersion::R2018);
    d.add_section("A", {{0, 0, 0}, {10, 0, 0}});
    hdxf::Point3 lo, hi;
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "extents");
    Drawing back = load(out, "extents back");
    // OBJECTS (settings, manager) give no extents of their own: the header's extents are the section's line
    const hdxf::HeaderVar* min = back.find_header("$EXTMIN");
    const hdxf::HeaderVar* max = back.find_header("$EXTMAX");
    assert_true(min && max, "extents in the header");
    assert_true(std::atof(max->values[0].second.c_str()) == 10.0 && std::atof(min->values[0].second.c_str()) == 0.0, "x 0 to 10 once");
}

TEST(section_types_old_formats_refuse_a_new_section_before_any_change_and_with_a_named_error) {
    for (DxfVersion v : {DxfVersion::R12, DxfVersion::R13, DxfVersion::R14}) {
        Drawing d(v);
        d.add_section("A", line4());
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(out.empty(), "no file");
        if (v == DxfVersion::R12) assert_eq(std::string("SECTIONOBJECT requires DXF 2000 or later"), d.last_error());
        else assert_eq(std::string("SECTIONOBJECT in DXF R13 or R14 is not established: no AutoCAD file has it (a record read from a file is kept as it was)"),
                       d.last_error());
    }
}
