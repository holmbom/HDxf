#include "test_framework.hpp"
#include "type_version_matrix.hpp"
#include "hdxf.hpp"
#include "typed_corpus.hpp"
#include "TypedRecordTypes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Step 9.12 of phase 9 (plan 2026-10-27-fas-9-typer.md): ACDBDETAILVIEWSTYLE and ACDBSECTIONVIEWSTYLE with their five parts.
//
// The AutoCAD originals: every doc/facit/acad_<version>_dxf_*.dxf has the template's Imperial24 of both kinds
// (doc/facit/acad_<version>_dxf_modeldoc_styles*.dxf from doc/facit/fas9_modeldoc_styles.scr is the one made for this step), and
// ACadSharp's six version pairs hold Imperial24 and Metric50. The core has no command that changes a view style (VIEWDETAILSTYLE,
// VIEWSECTIONSTYLE are "Unknown command"), so changed lines, labels, symbols and hatch angles, a style with an arrow symbol and the
// removal of a style have no AutoCAD file (F9.26): they are tested on the grammar of the records the files have and reported as ej testat.
// ---------------------------------------------------------------------------

using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::Object;
using hdxf::ObjectType;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;
using namespace hdxf::internal;

namespace {

using Group = std::pair<int, std::string>;
using Rec = std::vector<Group>;

const char* const kTags[] = {"2000", "2004", "2007", "2010", "2013", "2018"};

std::string facit_name(const char* theme, const char* tag, bool binary) {
    return std::string("acad_") + tag + "_dxf_" + theme + (binary ? "_bin" : "") + ".dxf";
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

bool is_real_code(int code) { return (code >= 10 && code <= 59) || (code >= 110 && code <= 149) || (code >= 210 && code <= 239); }

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

std::vector<Rec> style_records(const std::vector<uint8_t>& ascii) {
    std::vector<Rec> out;
    for (const Rec& r : ascii_records(ascii)) {
        if (!r.empty() && r[0].first == 0 && (r[0].second == "ACDBDETAILVIEWSTYLE" || r[0].second == "ACDBSECTIONVIEWSTYLE")) out.push_back(r);
    }
    return out;
}

void forget_styles(Drawing& d) {
    for (Object& o : d.objects()) {
        if (o.type == ObjectType::DetailViewStyle || o.type == ObjectType::SectionViewStyle) o.raw = hdxf::RawRecord();
    }
}

const Object* find_style(const Drawing& d, ObjectType type) {
    for (const Object& o : d.objects()) if (o.type == type) return &o;
    return nullptr;
}

bool warned(const Drawing& d, const std::string& part) {
    return std::any_of(d.warnings().begin(), d.warnings().end(), [&](const std::string& w) { return w.find(part) != std::string::npos; });
}

bool near(double a, double b) { return std::fabs(a - b) <= 1e-12 * std::max(1.0, std::fabs(a)); }

} // namespace

TEST(modeldoc_styles_the_registry_has_both_codecs_in_place_of_the_old_reader) {
    const TypedRegistry& registry = TypedRegistry::instance();
    for (const char* name : {"ACDBDETAILVIEWSTYLE", "ACDBSECTIONVIEWSTYLE"}) {
        const ObjectCodec* c = registry.find_object(name);
        assert_true(c != nullptr, name);
        assert_eq(12, c->step, "step 9.12");
        assert_true(c->read && c->write && c->references, "read, write, references");
        assert_eq(1025, c->cls.proxy_flags);
        assert_eq(1, static_cast<int>(c->lists.size()), "the parts are one unit");
        assert_true(c->versions.can_generate(DxfVersion::R2000, DxfFormat::Ascii) && !c->versions.can_generate(DxfVersion::R14, DxfFormat::Ascii), "2000 on");
    }
    assert_true(registry.find_object(ObjectType::DetailViewStyle) == registry.find_object("ACDBDETAILVIEWSTYLE"), "by type");
}

TEST(modeldoc_styles_autocads_template_styles_are_read_part_by_part_in_every_version_and_format) {
    for (const char* tag : kTags) {
        for (bool binary : {false, true}) {
            std::vector<uint8_t> in = facit_bytes(facit_name("modeldoc_styles", tag, binary));
            Drawing d = load(in, tag);
            const Object* detail = find_style(d, ObjectType::DetailViewStyle);
            const Object* section = find_style(d, ObjectType::SectionViewStyle);
            assert_true(detail && section, std::string(tag) + ": both styles typed");
            const hdxf::DetailViewStyleData& a = detail->as_detail_view_style();
            const hdxf::SectionViewStyleData& s = section->as_section_view_style();
            const bool colors = std::string(tag) != "2000";
            assert_eq(std::string("Imperial24"), a.base.name);
            assert_eq(std::string("Imperial24"), s.base.name);
            assert_eq(std::string("Imperial24"), a.base.display_name, "display name (300 in 2018, the XRECORD before)");
            assert_eq(3, a.flags, "detail part 0");
            assert_eq(78, s.flags, "section part 0");
            assert_true(near(a.identifier.height, 0.24) && near(a.identifier.arrow_size, 0.24) && near(a.identifier.offset, 0.36), "detail identifier");
            assert_eq(3, a.identifier.flags, "280");
            assert_eq(std::string("0"), a.identifier.arrow_handle, "no arrow symbol of its own");
            assert_eq(25, a.boundary_line.lineweight);
            assert_true(near(a.label.height, 0.24) && near(a.label.offset, 0.75) && a.label.position_flag == 1 && a.label.alignment_flag == 1, "detail label");
            assert_true(a.label.pattern.find("ViewDetailId") != std::string::npos, "the label's fields");
            assert_eq(25, a.lines[0].lineweight);
            assert_eq(0, a.end_flags);
            assert_eq(std::string("I, O, Q, S, X, Z"), s.identifier.excluded_characters);
            assert_true(near(s.identifier.offset, 0.48) && s.identifier.flags_a == 3 && near(s.identifier.distance, 0.18) && s.identifier.flags_b == 1, "section identifier");
            assert_eq(25, s.lines[0].lineweight);
            assert_eq(50, s.lines[1].lineweight);
            assert_true(near(s.measures[0], 0.24) && near(s.measures[1], 0.0) && near(s.measures[2], 0.24), "three measures");
            assert_eq(std::string("ANSI31"), s.hatch.pattern);
            assert_eq(6, static_cast<int>(s.hatch.angles.size()), "six angles in the template");
            assert_true(near(s.hatch.angles[1], 1.570796326794896), "an angle");
            // 62 from 2004 on (ByLayer 256; the hatch has 256 and 257), none in 2000
            assert_eq(colors, a.identifier.color.has_value(), "62 only from 2004");
            if (colors) {
                assert_eq(256, static_cast<int>(*a.identifier.color));
                assert_eq(256, static_cast<int>(*s.hatch.background_color));
                assert_eq(257, static_cast<int>(*s.hatch.foreground_color), "ByBlock");
            }
            assert_false(warned(d, "kept as it was read"), "nothing left untyped");
            std::vector<uint8_t> out = save(d, binary ? DxfFormat::Binary : DxfFormat::Ascii, tag);
            assert_true(out == in, std::string(tag) + (binary ? " binary" : " ASCII") + ": written unchanged is the file");
        }
    }
}

TEST(modeldoc_styles_the_typed_values_alone_write_autocads_own_records) {
    for (const char* tag : kTags) {
        std::vector<uint8_t> in = facit_bytes(facit_name("modeldoc_styles", tag, false));
        Drawing d = load(in, tag);
        forget_styles(d);
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, tag);
        std::vector<Rec> want = style_records(in), got = style_records(out);
        assert_eq(2, static_cast<int>(want.size()), "two styles");
        assert_eq(static_cast<int>(want.size()), static_cast<int>(got.size()), std::string(tag) + ": the same records");
        for (size_t i = 0; i < want.size(); ++i) {
            std::string why = differ(want[i], got[i]);
            assert_true(why.empty(), std::string(tag) + " " + want[i][0].second + ": " + why);
        }
    }
}

TEST(modeldoc_styles_acadsharps_metric50_pairs_in_six_versions_read_and_write_unchanged) {
    // ACadSharp's AutoCAD files have a second style of each kind (Metric50): geoloc.dxf and sample_AC1015 ... AC1032
    for (const char* file : {"acadsharp/geoloc.dxf", "acadsharp/geoloc_AC1021.dxf", "acadsharp/sample_AC1015_ascii.dxf", "acadsharp/sample_AC1027_ascii.dxf"}) {
        std::filesystem::path p = hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(file);
        std::vector<uint8_t> in = hdxf_test::read_file_bytes(p);
        if (in.empty()) hdxf_test::skip(std::string("missing ") + file);
        Drawing d = load(in, file);
        int n = 0;
        for (const Object& o : d.objects()) {
            if (o.type == ObjectType::DetailViewStyle || o.type == ObjectType::SectionViewStyle) ++n;
        }
        assert_true(n >= 2, std::string(file) + ": the styles typed");
        assert_false(warned(d, "ACDBDETAILVIEWSTYLE") || warned(d, "ACDBSECTIONVIEWSTYLE"), std::string(file) + ": none left untyped");
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), d.last_error());
        // the typed values alone
        forget_styles(d);
        std::vector<uint8_t> alone = d.save_to_memory(DxfFormat::Ascii);
        std::vector<Rec> want = style_records(in), got = style_records(alone);
        assert_eq(static_cast<int>(want.size()), static_cast<int>(got.size()), "records");
        for (size_t i = 0; i < want.size(); ++i) {
            std::string why = differ(want[i], got[i]);
            assert_true(why.empty(), std::string(file) + " " + want[i][0].second + ": " + why);
        }
    }
}

TEST(modeldoc_styles_a_changed_value_is_one_changed_group_and_a_list_longer_than_six_is_kept) {
    std::vector<uint8_t> in = facit_bytes(facit_name("modeldoc_styles", "2018", false));
    Drawing d = load(in, "2018");
    Object* detail = nullptr;
    Object* section = nullptr;
    for (Object& o : d.objects()) {
        if (o.type == ObjectType::DetailViewStyle) detail = &o;
        if (o.type == ObjectType::SectionViewStyle) section = &o;
    }
    detail->as_detail_view_style().label.height = 0.5;
    detail->as_detail_view_style().lines[1].lineweight = 40;
    section->as_section_view_style().hatch.angles.push_back(0.125);
    section->as_section_view_style().hatch.angles.push_back(0.25);
    section->as_section_view_style().hatch.pattern = "ANSI37 \xC3\xA5\xC3\xA4\xC3\xB6";
    section->as_section_view_style().identifier.excluded_characters = "A, B";
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "changed");
    Drawing back = load(out, "changed back");
    const hdxf::DetailViewStyleData& a = find_style(back, ObjectType::DetailViewStyle)->as_detail_view_style();
    const hdxf::SectionViewStyleData& s = find_style(back, ObjectType::SectionViewStyle)->as_section_view_style();
    assert_true(near(a.label.height, 0.5) && a.lines[1].lineweight == 40 && a.lines[0].lineweight == 25, "the detail's change");
    assert_eq(8, static_cast<int>(s.hatch.angles.size()), "eight angles, the count follows (90)");
    assert_eq(std::string("ANSI37 \xC3\xA5\xC3\xA4\xC3\xB6"), s.hatch.pattern);
    assert_eq(std::string("A, B"), s.identifier.excluded_characters);
    assert_true(near(s.hatch.angles[7], 0.25) && near(s.hatch.angles[5], 1.832595714594046), "the old ones stay");
    // the base and the other records are as they were
    assert_eq(std::string("Imperial24"), a.base.display_name);
    assert_true(save(back, DxfFormat::Ascii, "again") == out, "B12");
    // fewer: the count follows too
    Object* s2 = nullptr;
    for (Object& o : back.objects()) if (o.type == ObjectType::SectionViewStyle) s2 = &o;
    s2->as_section_view_style().hatch.angles.clear();
    Drawing none = load(save(back, DxfFormat::Ascii, "none"), "none back");
    assert_eq(0, static_cast<int>(find_style(none, ObjectType::SectionViewStyle)->as_section_view_style().hatch.angles.size()), "no angles");
}

TEST(modeldoc_styles_the_display_name_stays_the_base_s_job_in_every_version) {
    for (const char* tag : {"2000", "2013", "2018"}) {
        std::vector<uint8_t> in = facit_bytes(facit_name("modeldoc_styles", tag, false));
        Drawing d = load(in, tag);
        Object* o = nullptr;
        for (Object& x : d.objects()) if (x.type == ObjectType::SectionViewStyle) o = &x;
        o->as_view_style().display_name = "Sektion \xC3\xA5";
        o->as_view_style().display_flags = 7;
        o->as_section_view_style().flags = 5;
        Drawing back = load(save(d, DxfFormat::Ascii, tag), tag);
        const Object* b = find_style(back, ObjectType::SectionViewStyle);
        assert_eq(std::string("Sektion \xC3\xA5"), b->as_view_style().display_name, std::string(tag) + ": display name");
        assert_eq(7, b->as_view_style().display_flags);
        assert_eq(5, b->as_section_view_style().flags, "the derived part");
    }
}

TEST(modeldoc_styles_new_styles_in_every_version_and_format) {
    const TypedRegistry& registry = TypedRegistry::instance();
    for (const char* name : {"ACDBDETAILVIEWSTYLE", "ACDBSECTIONVIEWSTYLE"}) {
        const bool detail = std::string(name) == "ACDBDETAILVIEWSTYLE";
        auto add = [detail](Drawing& d) {
            Object o;
            o.type = detail ? ObjectType::DetailViewStyle : ObjectType::SectionViewStyle;
            o.owner = "0";
            if (detail) {
                hdxf::DetailViewStyleData s;
                s.base.name = "Mine";
                s.base.display_name = "Mine";
                s.identifier.height = 0.5;
                s.label.pattern = "A";
                o.data = hdxf::Boxed<hdxf::DetailViewStyleData>(s);
            } else {
                hdxf::SectionViewStyleData s;
                s.base.name = "Mine";
                s.base.display_name = "Mine";
                s.identifier.height = 0.5;
                s.hatch.pattern = "ANSI31";
                s.hatch.angles = {0.0, 0.5, 1.0};
                o.data = hdxf::Boxed<hdxf::SectionViewStyleData>(s);
            }
            d.add_line({0, 0, 0}, {1, 1, 0});
            d.objects().push_back(o);
        };
        auto verify = [detail](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            for (const Object& o : r.objects()) {
                if (o.type == (detail ? ObjectType::DetailViewStyle : ObjectType::SectionViewStyle) && o.as_view_style().name == "Mine") {
                    if (detail && o.as_detail_view_style().identifier.height != 0.5) return "the height";
                    if (!detail && o.as_section_view_style().hatch.angles.size() != 3) return "the angles";
                    return {};
                }
            }
            return "the style";
        };
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix(name, registry.find_object(name)->versions, add, verify);
        assert_eq(12, report.generated, std::string(name) + ": 2000 ... 2018");
        assert_eq(6, report.named_errors, std::string(name) + ": R12, R13, R14");
        std::cout << "  " << name << ": " << report.generated << " cells generated, " << report.named_errors << " named errors, AutoCAD verified "
                  << report.autocad_verified << ", ej testat " << report.autocad_not_tested << ", R13/R14 not savable " << report.autocad_not_savable << "\n";
    }
}

TEST(modeldoc_styles_a_removed_target_is_zero_with_a_warning_and_a_nonzero_arrow_is_kept_b19) {
    std::vector<uint8_t> in = facit_bytes(facit_name("modeldoc_styles", "2018", false));
    Drawing d = load(in, "2018");
    // A layer that is in the file, then removed: the style's pointer to it leads nowhere (B19; a handle that led nowhere
    // already in the file is kept as it was)
    d.add_layer("HDXF_GONE");
    d = load(save(d, DxfFormat::Ascii, "with a layer"), "with a layer");
    std::string gone;
    for (const hdxf::Layer& l : d.layers()) if (l.name == "HDXF_GONE") gone = l.handle;
    assert_false(gone.empty(), "the layer's handle");
    Object* detail = nullptr;
    for (Object& o : d.objects()) if (o.type == ObjectType::DetailViewStyle) detail = &o;
    detail->as_detail_view_style().identifier.style_handle = gone;
    assert_true(d.remove_layer("HDXF_GONE"), "removed");
    // an arrow block that is there (the drawing's model space record): kept
    std::string arrow;
    for (const hdxf::Block& b : d.blocks()) if (b.name == "*Model_Space") arrow = b.record.handle;
    assert_false(arrow.empty(), "the model space record");
    detail->as_detail_view_style().identifier.arrow_handle = arrow;
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "removed");
    Drawing back = load(out, "removed back");
    const hdxf::DetailViewStyleData& a = find_style(back, ObjectType::DetailViewStyle)->as_detail_view_style();
    assert_eq(std::string("0"), a.identifier.style_handle, "340 is none (0): nothing dangles");
    assert_eq(arrow, a.identifier.arrow_handle, "a non-zero arrow handle is kept");
    assert_true(warned(d, "no longer exists"), "a warning");
}

TEST(modeldoc_styles_records_that_do_not_fit_stay_untyped_and_preserved_with_a_warning) {
    std::vector<uint8_t> in = facit_bytes(facit_name("modeldoc_styles", "2018", false));
    std::string text(in.begin(), in.end());
    // part 2 begins with the wrong number
    std::string bad = text;
    size_t at = bad.find("100\r\nAcDbDetailViewStyle");
    assert_true(at != std::string::npos, "the subclass");
    size_t p = bad.find("71\r\n     2\r\n", at);
    assert_true(p != std::string::npos, "part 2");
    bad.replace(p, 12, "71\r\n     9\r\n");
    std::vector<uint8_t> bytes(bad.begin(), bad.end());
    Drawing d;
    assert_true(d.load_from_memory(bytes.data(), bytes.size()), d.last_error());
    assert_true(warned(d, "ACDBDETAILVIEWSTYLE"), "a warning names the record");
    assert_true(find_style(d, ObjectType::DetailViewStyle) == nullptr && find_style(d, ObjectType::SectionViewStyle) != nullptr, "one left untyped");
    assert_true(d.save_to_memory(DxfFormat::Ascii) == bytes, "kept byte for byte");
    // truncated: the hatch's angle count says more than there are
    std::string many = text;
    size_t h = many.find("100\r\nAcDbSectionViewStyle");
    size_t c = many.find("90\r\n        6\r\n", many.find("ANSI31", h));
    assert_true(c != std::string::npos, "the angle count");
    many.replace(c, 15, "90\r\n     9000\r\n");
    std::vector<uint8_t> b2(many.begin(), many.end());
    Drawing e;
    assert_true(e.load_from_memory(b2.data(), b2.size()), e.last_error());
    assert_true(warned(e, "ACDBSECTIONVIEWSTYLE"), "the section style is named");
    assert_true(e.save_to_memory(DxfFormat::Ascii) == b2, "kept byte for byte");
}
