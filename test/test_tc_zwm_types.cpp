#include "test_framework.hpp"
#include "type_version_matrix.hpp"
#include "hdxf.hpp"
#include "typed_corpus.hpp"
#include "TypedRecordTypes.hpp"
#include "proxy_graphics.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Step 9.18 of phase 9 (plan 2026-10-27-fas-9-typer.md): TurboCAD's TCOPTIONALDATA and TCPOINTENTITY and ZWCAD's 28 ZWM*STD records.
//
// The originals are third-party drawings (build/test-data/local-extensions/PROVENANCE.txt): a ZWCAD drawing (AC1024) with every ZWM
// record once, and TurboCAD drawings of AC1014, AC1018 and AC1021 with the 17 forms of TCOPTIONALDATA and a TCPOINTENTITY. They are
// observations, not AutoCAD facit: AutoCAD has not saved them (F9.25: ej testat for every other branch, for AutoCAD's own writing of
// them, and for ZWMSTANDARD's 70 of 483351136 in binary). The core has no plugin: no command changes one of these records.
// ---------------------------------------------------------------------------

using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::Entity;
using hdxf::EntityType;
using hdxf::Object;
using hdxf::ObjectType;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;
using namespace hdxf::internal;

namespace {

using Group = std::pair<int, std::string>;
using Rec = std::vector<Group>;

std::vector<uint8_t> local_bytes(const std::string& name) {
    std::filesystem::path p = hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / "local-extensions" / hdxf::path_from_utf8(name);
    std::vector<uint8_t> in = hdxf_test::read_file_bytes(p);
    if (in.empty()) hdxf_test::skip("missing build/test-data/local-extensions/" + name);
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
        if (!out.empty()) out.back().push_back({code, trim(value_line)});
    }
    return out;
}

// The records whose type name starts with `prefix`, from their first subclass on (the head's reactors and owner are the framework's)
std::vector<Rec> records_of(const std::vector<uint8_t>& ascii, const std::string& prefix) {
    std::vector<Rec> out;
    for (const Rec& r : ascii_records(ascii)) {
        if (r.empty() || r[0].first != 0 || r[0].second.compare(0, prefix.size(), prefix) != 0) continue;
        Rec body;
        bool in_body = false;
        for (const Group& g : r) {
            if (g.first == 100 && !in_body) { in_body = true; body.push_back(r[0]); }
            if (in_body) body.push_back(g);
        }
        if (in_body) out.push_back(body);
    }
    return out;
}

bool is_real_code(int code) { return (code >= 10 && code <= 59) || (code >= 110 && code <= 149) || (code >= 210 && code <= 239); }

std::string differ(const Rec& a, const Rec& b) {
    if (a.size() != b.size()) return "group counts " + std::to_string(a.size()) + " and " + std::to_string(b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].first != b[i].first) return "group " + std::to_string(i) + ": code " + std::to_string(a[i].first) + " and " + std::to_string(b[i].first);
        if (a[i].second == b[i].second) continue;
        if (is_real_code(a[i].first)) {
            double p = std::atof(a[i].second.c_str()), q = std::atof(b[i].second.c_str());
            if (std::fabs(p - q) <= 1e-12 * std::max(1.0, std::fabs(p))) continue;
        }
        return "group " + std::to_string(i) + " (" + std::to_string(a[i].first) + "): \"" + a[i].second + "\" and \"" + b[i].second + "\"";
    }
    return {};
}

bool is_zwm(const Object& o) { return o.type >= ObjectType::ZwmBalloonStd && o.type <= ObjectType::ZwmWeldingStd; }

const Object* find(const Drawing& d, ObjectType t) {
    for (const Object& o : d.objects()) if (o.type == t) return &o;
    return nullptr;
}
Object* find_mut(Drawing& d, ObjectType t) {
    for (Object& o : d.objects()) if (o.type == t) return &o;
    return nullptr;
}

bool erase_object(Drawing& d, const std::string& handle) {
    std::vector<Object>& list = d.objects();
    auto it = std::find_if(list.begin(), list.end(), [&](const Object& o) { return o.handle == handle; });
    if (it == list.end()) return false;
    list.erase(it);
    return true;
}

bool warned(const Drawing& d, const std::string& part) {
    return std::any_of(d.warnings().begin(), d.warnings().end(), [&](const std::string& w) { return w.find(part) != std::string::npos; });
}

const char* const kZwmFile = "zwm_hexa_outline.dxf";

const char* const kZwmNames[] = {
    "ZWMBALLOONSTD", "ZWMBOMSTYLESTD", "ZWMBREAKSYMSTD", "ZWMCENTERHOLESTD", "ZWMCHAMFERSTD", "ZWMCIRCLEMARKSTD", "ZWMCOLORSTD",
    "ZWMDATUMIDSTD", "ZWMDATUMTARGETSTD", "ZWMDETAILSTD", "ZWMDIMSTYLESTD", "ZWMELEVSYMSTD", "ZWMFCFRAMESTD", "ZWMFEATUREIDSTD",
    "ZWMFRAMESTYLESTD", "ZWMHOLECHARTSTD", "ZWMLAYERSTD", "ZWMLINETYPESTD", "ZWMNOTESTD", "ZWMPARTREFSTD", "ZWMSECTIONSYMSTD",
    "ZWMSTANDARD", "ZWMSURFSYMSTD", "ZWMTAPERSLOPESTD", "ZWMTEXTSTYLESTD", "ZWMTITLESTYLESTD", "ZWMVIEWDIRECTIONSTD", "ZWMWELDINGSTD"};

} // namespace

TEST(tc_zwm_the_registry_has_a_codec_for_every_name_with_its_class) {
    const TypedRegistry& registry = TypedRegistry::instance();
    for (const char* name : kZwmNames) {
        const ObjectCodec* c = registry.find_object(name);
        assert_true(c != nullptr, name);
        assert_eq(18, c->step, "step 9.18");
        assert_true(c->read && c->write && c->references, "read, write, references");
        assert_eq(std::string("ZWMCUSTCORE"), std::string(c->cls.application));
        assert_eq(1, c->cls.proxy_flags);
        assert_true(c->versions.can_generate(DxfVersion::R2010, DxfFormat::Binary) && !c->versions.can_generate(DxfVersion::R14, DxfFormat::Ascii), "2000 on");
    }
    assert_eq(std::string("ZwmLineTypeStd"), std::string(registry.find_object("ZWMLINETYPESTD")->cls.cpp_name), "the class's spelling");
    assert_eq(std::string("ZwmFCFrameStd"), std::string(registry.find_object("ZWMFCFRAMESTD")->cls.cpp_name));
    const ObjectCodec* tc = registry.find_object("TCOPTIONALDATA");
    assert_true(tc != nullptr && tc->step == 18, "TCOPTIONALDATA");
    assert_eq(4095, tc->cls.proxy_flags);
    assert_true(tc->cls.in_older, "TurboCAD's R14 files have the class");
    assert_true(tc->versions.can_generate(DxfVersion::R14, DxfFormat::Ascii) && !tc->versions.can_generate(DxfVersion::R13, DxfFormat::Ascii), "R14 yes, R13 no");
    const EntityCodec* point = registry.find_entity("TCPOINTENTITY");
    assert_true(point != nullptr && point->extents != nullptr, "TCPOINTENTITY has an extents function");
    assert_true(registry.find_entity(EntityType::TcPoint) == point, "by type");
}

TEST(tc_zwm_the_zwcad_drawing_is_read_component_by_component_and_written_unchanged) {
    std::vector<uint8_t> in = local_bytes(kZwmFile);
    Drawing d = load(in, kZwmFile);
    int n = 0;
    for (const Object& o : d.objects()) if (is_zwm(o)) ++n;
    assert_eq(28, n, "every ZWM record typed");
    assert_false(warned(d, "kept as they were read"), "nothing left untyped");
    const hdxf::ZwmBalloonStdData& balloon = find(d, ObjectType::ZwmBalloonStd)->as_zwm_balloon_std();
    assert_eq(std::string("ISO"), balloon.header.text_1[1]);
    assert_eq(std::string("ISO"), balloon.standard.text_1[1]);
    assert_eq(2, static_cast<int>(balloon.symbol.attributes.size()), "two attribute definitions");
    assert_eq(std::string("EnableExplode"), balloon.symbol.attributes[0].key);
    assert_eq(std::string("VT_BOOL"), balloon.symbol.attributes[0].definition.text_1[3]);
    assert_eq(-1, static_cast<int>(balloon.symbol.attributes[0].definition.value.integer), "VT_BOOL true");
    assert_eq(std::string("SymTextSizeRatio"), balloon.symbol.attributes[1].key);
    assert_true(balloon.symbol.attributes[1].definition.value.real == 1.0, "VT_R8");
    assert_eq(42, static_cast<int>(balloon.symbol.list_70_a.size()), "the 42 ids");
    assert_eq(7, static_cast<int>(balloon.symbol.list_1_a.size()), "the sizes");
    assert_eq(2, static_cast<int>(balloon.properties.shapes.size()), "Table and star");
    assert_eq(std::string("Table.dwg"), balloon.properties.shapes[0].text_1_2);
    assert_eq(91, static_cast<int>(balloon.symbol.reserved_70_0));
    const hdxf::ZwmLayerStdData& layer = find(d, ObjectType::ZwmLayerStd)->as_zwm_layer_std();
    assert_eq(41, static_cast<int>(layer.attributes.size()), "ZwmObject counts the definitions");
    assert_eq(41, static_cast<int>(layer.layer.layers.size()));
    assert_eq(std::string("ZWMLY_DATUMID_ARROW"), layer.layer.layers[0].text_1_0);
    assert_eq(std::string("ZWMLY_THICK0"), layer.attributes[0].text_1[0]);
    assert_eq(-9999, static_cast<int>(layer.attributes[0].reserved_70_1[9]));
    const hdxf::ZwmStandardData& standard = find(d, ObjectType::ZwmStandard)->as_zwm_standard();
    assert_eq(483351136, standard.symbol.reserved_70_1, "outside the 16 bits");
    assert_eq(27, static_cast<int>(standard.properties.owned.size()), "the records it owns (360)");
    assert_eq(30, static_cast<int>(standard.properties.names.size()));
    assert_eq(std::string("ZWMCL_CYAN"), standard.properties.names[0].text_1_0);
    assert_eq(10, static_cast<int>(standard.properties.names[0].reserved_70_0));
    assert_eq(0, static_cast<int>(standard.symbol.attributes.size()));
    const hdxf::ZwmHoleChartStdData& chart = find(d, ObjectType::ZwmHoleChartStd)->as_zwm_hole_chart_std();
    const size_t counts[6] = {7, 7, 4, 7, 7, 4};
    for (size_t i = 0; i < 6; ++i) assert_eq(static_cast<int>(counts[i]), static_cast<int>(chart.properties.rows[i].size()), "a pair list of the table");
    assert_eq(37, static_cast<int>(chart.symbol.attributes.size()), "37 definitions of three value types");
    const hdxf::ZwmFrameStyleStdData& frame = find(d, ObjectType::ZwmFrameStyleStd)->as_zwm_frame_style_std();
    assert_eq(2, static_cast<int>(frame.style.frames.size()));
    assert_eq(std::string("A3_Partition_landscape"), frame.style.frames[0].name);
    assert_eq(1, static_cast<int>(frame.style.frames[0].reserved_70_0), "the 70 that ends the first frame");
    assert_eq(std::string("A3_Partition_portrait"), frame.style.frames[1].name);
    assert_true(frame.style.frames[0].properties.reserved_40[0] == 420.0, "the sheet's width");
    const hdxf::ZwmTitleStyleStdData& title = find(d, ObjectType::ZwmTitleStyleStd)->as_zwm_title_style_std();
    assert_eq(19, static_cast<int>(title.properties.attributes.size()));
    assert_eq(std::string("FILENAME"), title.properties.attributes[0].definition.text_1[0]);
    assert_true(title.properties.attributes[15].definition.value.real == 0.0 && title.properties.attributes[15].definition.text_1[3] == "VT_R8", "the one real");
    assert_eq(5, static_cast<int>(find(d, ObjectType::ZwmBomStyleStd)->as_zwm_bom_style_std().properties.attributes.size()), "five columns");
    assert_eq(30, static_cast<int>(find(d, ObjectType::ZwmColorStd)->as_zwm_color_std().attributes.size()));
    assert_eq(9, static_cast<int>(find(d, ObjectType::ZwmDimStyleStd)->as_zwm_dim_style_std().dim_style.styles.size()));
    assert_eq(8, static_cast<int>(find(d, ObjectType::ZwmLineTypeStd)->as_zwm_line_type_std().linetype.linetypes.size()));
    assert_eq(9, static_cast<int>(find(d, ObjectType::ZwmTextStyleStd)->as_zwm_text_style_std().text_style.styles.size()));
    assert_eq(36, static_cast<int>(find(d, ObjectType::ZwmCenterHoleStd)->as_zwm_center_hole_std().properties.list_1_a.size()));
    assert_eq(39, static_cast<int>(find(d, ObjectType::ZwmDetailStd)->as_zwm_detail_std().properties.list_1_b.size()));
    assert_eq(std::string("FFFF"), find(d, ObjectType::ZwmDetailStd)->as_zwm_detail_std().properties.handle_340_0, "a pointer that leads nowhere in the file");
    assert_eq(15, static_cast<int>(find(d, ObjectType::ZwmFCFrameStd)->as_zwm_fc_frame_std().properties.rows_a.size()));
    assert_eq(12, static_cast<int>(find(d, ObjectType::ZwmFCFrameStd)->as_zwm_fc_frame_std().properties.rows_b.size()));
    assert_eq(24, static_cast<int>(find(d, ObjectType::ZwmWeldingStd)->as_zwm_welding_std().properties.rows_a.size()));
    assert_eq(1, static_cast<int>(find(d, ObjectType::ZwmSurfSymStd)->as_zwm_surf_sym_std().symbol.reserved_70_1 == 1 ? 1 : 0));

    // B12: written as read, the records are the file's, byte for byte (ZWMSTANDARD's 483351136 included)
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "unchanged");
    std::vector<Rec> want = records_of(in, "ZWM"), got = records_of(out, "ZWM");
    assert_eq(28, static_cast<int>(want.size()));
    assert_eq(28, static_cast<int>(got.size()));
    for (size_t i = 0; i < want.size(); ++i) assert_true(want[i] == got[i], want[i][0].second + ": unchanged is the file's");
}

TEST(tc_zwm_the_typed_values_alone_write_the_files_records_and_zwmstandards_wide_70_is_a_named_error) {
    std::vector<uint8_t> in = local_bytes(kZwmFile);
    Drawing d = load(in, kZwmFile);
    // every record but ZWMSTANDARD from its typed values alone
    for (Object& o : d.objects()) if (is_zwm(o) && o.type != ObjectType::ZwmStandard) o.raw = hdxf::RawRecord();
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "typed alone");
    std::vector<Rec> want = records_of(in, "ZWM"), got = records_of(out, "ZWM");
    assert_eq(static_cast<int>(want.size()), static_cast<int>(got.size()));
    for (size_t i = 0; i < want.size(); ++i) {
        std::string why = differ(want[i], got[i]);
        assert_true(why.empty(), want[i][0].second + ": " + why);
    }
    // ZWMSTANDARD alone: the 70 does not fit the 16 bits of a group; it is not truncated
    find_mut(d, ObjectType::ZwmStandard)->raw = hdxf::RawRecord();
    assert_true(d.save_to_memory(DxfFormat::Ascii).empty(), "a new record with it is refused");
    assert_true(d.last_error().find("ZWMSTANDARD") != std::string::npos && d.last_error().find("483351136") != std::string::npos &&
                d.last_error().find("16 bits") != std::string::npos, "the named error: " + d.last_error());
    // a record read from the file, as binary
    Drawing e = load(in, kZwmFile);
    assert_true(e.save_to_memory(DxfFormat::Binary).empty(), "binary cannot hold it");
    assert_true(e.last_error().find("483351136") != std::string::npos, "named: " + e.last_error());
    // a changed value that fits is written, a changed one that does not is refused even in ASCII
    Drawing f = load(in, kZwmFile);
    find_mut(f, ObjectType::ZwmStandard)->as_zwm_standard().symbol.reserved_70_1 = 5;
    std::vector<uint8_t> small = save(f, DxfFormat::Ascii, "fits");
    assert_true(save(f, DxfFormat::Binary, "binary fits").size() > 0, "and in binary");
    Drawing g = load(small, "fits back");
    assert_eq(5, find(g, ObjectType::ZwmStandard)->as_zwm_standard().symbol.reserved_70_1);
    Drawing h = load(in, kZwmFile);
    find_mut(h, ObjectType::ZwmStandard)->as_zwm_standard().symbol.reserved_70_1 = 483351137;
    assert_true(h.save_to_memory(DxfFormat::Ascii).empty(), "a different wide value is new");
    assert_true(h.last_error().find("483351137") != std::string::npos, h.last_error());
}

TEST(tc_zwm_a_changed_value_and_longer_lists_keep_their_counts) {
    std::vector<uint8_t> in = local_bytes(kZwmFile);
    Drawing d = load(in, kZwmFile);
    hdxf::ZwmBalloonStdData& b = find_mut(d, ObjectType::ZwmBalloonStd)->as_zwm_balloon_std();
    b.standard.text_1[1] = "ISO \xC3\xA5";
    b.symbol.list_70_a.push_back(777);
    b.symbol.list_1_a.push_back("28");
    hdxf::ZwmAttributeEntry extra = b.symbol.attributes[1];
    extra.key = "ExtraRatio";
    extra.definition.value.real = 2.5;
    b.symbol.attributes.push_back(extra);
    b.properties.shapes.push_back({"circle", "circle", "circle.dwg", 1});
    hdxf::ZwmLayerStdData& l = find_mut(d, ObjectType::ZwmLayerStd)->as_zwm_layer_std();
    l.attributes.pop_back();
    l.layer.layers.push_back({"ZWMLY_NEW", "AM_9"});
    hdxf::ZwmStandardData& s = find_mut(d, ObjectType::ZwmStandard)->as_zwm_standard();
    s.properties.names.push_back({"ZWMCL_NEW", 12});
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "changed");
    Drawing back = load(out, "changed back");
    const hdxf::ZwmBalloonStdData& bb = find(back, ObjectType::ZwmBalloonStd)->as_zwm_balloon_std();
    assert_eq(std::string("ISO \xC3\xA5"), bb.standard.text_1[1]);
    assert_eq(43, static_cast<int>(bb.symbol.list_70_a.size()), "the count follows");
    assert_eq(777, static_cast<int>(bb.symbol.list_70_a[42]));
    assert_eq(8, static_cast<int>(bb.symbol.list_1_a.size()));
    assert_eq(3, static_cast<int>(bb.symbol.attributes.size()), "three definitions, each with its key");
    assert_eq(std::string("ExtraRatio"), bb.symbol.attributes[2].key);
    assert_true(bb.symbol.attributes[2].definition.value.real == 2.5, "a VT_R8 value");
    assert_eq(3, static_cast<int>(bb.properties.shapes.size()));
    const hdxf::ZwmLayerStdData& ll = find(back, ObjectType::ZwmLayerStd)->as_zwm_layer_std();
    assert_eq(40, static_cast<int>(ll.attributes.size()), "ZwmObject's count follows");
    assert_eq(42, static_cast<int>(ll.layer.layers.size()));
    assert_eq(31, static_cast<int>(find(back, ObjectType::ZwmStandard)->as_zwm_standard().properties.names.size()));
    // the other records are as they were
    std::vector<Rec> was = records_of(in, "ZWM"), now = records_of(out, "ZWM");
    assert_eq(static_cast<int>(was.size()), static_cast<int>(now.size()));
    for (size_t i = 0; i < was.size(); ++i) {
        const std::string& name = was[i][0].second;
        if (name == "ZWMBALLOONSTD" || name == "ZWMLAYERSTD" || name == "ZWMSTANDARD") continue;
        assert_true(was[i] == now[i], name + " untouched");
    }
    // ZWMSTANDARD's wide 70 is still the file's
    for (size_t i = 0; i < was.size(); ++i) {
        if (was[i][0].second != "ZWMSTANDARD") continue;
        bool wide = false;
        for (const Group& g : now[i]) wide = wide || g.second == "483351136";
        assert_true(wide, "the wide 70 stays as it was");
    }
    assert_true(save(back, DxfFormat::Ascii, "again") == out, "written again unchanged");
}

TEST(tc_zwm_the_record_a_standard_owns_is_taken_out_of_its_list_when_it_is_removed_b19) {
    std::vector<uint8_t> in = local_bytes(kZwmFile);
    Drawing d = load(in, kZwmFile);
    const std::string gone = find(d, ObjectType::ZwmStandard)->as_zwm_standard().properties.owned[0];
    assert_true(erase_object(d, gone), "removed " + gone);
    Drawing back = load(save(d, DxfFormat::Ascii, "removed"), "removed back");
    const hdxf::ZwmStandardData& s = find(back, ObjectType::ZwmStandard)->as_zwm_standard();
    assert_eq(26, static_cast<int>(s.properties.owned.size()), "the member and the count (70) go");
    assert_true(std::find(s.properties.owned.begin(), s.properties.owned.end(), gone) == s.properties.owned.end(), "gone");
    assert_eq(30, static_cast<int>(s.properties.names.size()), "the names are the file's own");
    assert_eq(483351136, s.symbol.reserved_70_1, "the wide 70 stays");
    // the pointer to FFFF led nowhere in the file already: kept
    assert_eq(std::string("FFFF"), find(back, ObjectType::ZwmDetailStd)->as_zwm_detail_std().properties.handle_340_0);
}

TEST(tc_zwm_records_that_do_not_fit_stay_untyped_and_preserved_with_a_warning) {
    std::vector<uint8_t> in = local_bytes(kZwmFile);
    std::string text(in.begin(), in.end());
    // an attribute definition of a value type that is not established
    std::string bad = text;
    size_t at = bad.find("VT_R8");
    assert_true(at != std::string::npos, "a VT_R8");
    bad.replace(at, 5, "VT_I4");
    std::vector<uint8_t> bytes(bad.begin(), bad.end());
    Drawing d;
    assert_true(d.load_from_memory(bytes.data(), bytes.size()), d.last_error());
    assert_true(warned(d, "ZWM"), "a warning names a ZWM record");
    int n = 0;
    for (const Object& o : d.objects()) if (is_zwm(o)) ++n;
    assert_true(n < 28 && n > 0, "the records with that value type are left untyped, the others typed");
    assert_true(d.save_to_memory(DxfFormat::Ascii).size() == bytes.size(), "kept");
    assert_true(d.save_to_memory(DxfFormat::Ascii) == bytes, "kept byte for byte");
    // a count that says more than there are
    std::string many = text;
    size_t c = many.find("ZwmCenterHoleProperties");
    assert_true(c != std::string::npos, "the properties");
    size_t k = many.find("    36", c);
    assert_true(k != std::string::npos && k - c < 80, "the count of 36");
    many.replace(k, 6, "  9000");
    std::vector<uint8_t> b2(many.begin(), many.end());
    Drawing e;
    assert_true(e.load_from_memory(b2.data(), b2.size()), e.last_error());
    assert_true(warned(e, "ZWMCENTERHOLESTD"), "the record is named");
    assert_true(find(e, ObjectType::ZwmCenterHoleStd) == nullptr && find(e, ObjectType::ZwmBalloonStd) != nullptr, "one left untyped");
    assert_true(e.save_to_memory(DxfFormat::Ascii) == b2, "kept byte for byte");
}

namespace {

struct ZwmSpec {
    const char* name;
    std::function<void(Object&)> fill;
    std::function<std::string(const Object&)> check;
};

// One new record of a ZWM type: its names changed, definitions and list entries where the type has them
template <typename D>
ZwmSpec zwm_spec(const char* name) {
    ZwmSpec spec;
    spec.name = name;
    spec.fill = [](Object& o) {
        D d;
        d.header.text_1[1] = "ISO \xC3\xA5";
        d.standard.text_1[1] = "Mine";
        if constexpr (requires { d.symbol.attributes; }) {
            hdxf::ZwmAttributeEntry real;
            real.key = "Ratio";
            real.definition.text_1[3] = "VT_R8";
            real.definition.value.real = 2.5;
            hdxf::ZwmAttributeEntry flag;
            flag.key = "Explode";
            flag.definition.text_1[3] = "VT_BOOL";
            flag.definition.value.integer = -1;
            hdxf::ZwmAttributeEntry text;
            text.key = "Label";
            text.definition.value.text = "L";
            d.symbol.attributes = {real, flag, text};
            d.symbol.list_70_a = {1, 2, 3};
            d.symbol.list_1_a = {"2.5", "3.5"};
        }
        if constexpr (requires { d.attributes; }) {
            hdxf::ZwmAttributeDefinition a;
            a.text_1[0] = "NAME";
            a.value.text = "v";
            d.attributes = {a, a};
        }
        o.data = hdxf::Boxed<D>(std::move(d));
    };
    spec.check = [](const Object& o) -> std::string {
        const D& d = *o.data.get<hdxf::Boxed<D>>();
        if (d.header.text_1[1] != "ISO \xC3\xA5") return "the header's name";
        if (d.standard.text_1[1] != "Mine") return "ZwmStd's name";
        if constexpr (requires { d.symbol.attributes; }) {
            if (d.symbol.attributes.size() != 3) return "the attribute definitions";
            if (d.symbol.attributes[0].key != "Ratio" || d.symbol.attributes[0].definition.value.real != 2.5) return "a real value";
            if (d.symbol.attributes[1].definition.value.integer != -1) return "a boolean value";
            if (d.symbol.attributes[2].definition.value.text != "L") return "a text value";
            if (d.symbol.list_70_a.size() != 3 || d.symbol.list_1_a.size() != 2) return "the symbol's lists";
        }
        if constexpr (requires { d.attributes; }) {
            if (d.attributes.size() != 2 || d.attributes[1].text_1[0] != "NAME") return "the definitions after ZwmObject";
        }
        return {};
    };
    return spec;
}

} // namespace

TEST(tc_zwm_new_records_of_every_zwm_type_in_every_version_and_format) {
    const TypedRegistry& registry = TypedRegistry::instance();
    std::vector<ZwmSpec> specs;
    specs.push_back(zwm_spec<hdxf::ZwmBalloonStdData>("ZWMBALLOONSTD"));
    specs.push_back(zwm_spec<hdxf::ZwmBomStyleStdData>("ZWMBOMSTYLESTD"));
    specs.push_back(zwm_spec<hdxf::ZwmBreakSymStdData>("ZWMBREAKSYMSTD"));
    specs.push_back(zwm_spec<hdxf::ZwmCenterHoleStdData>("ZWMCENTERHOLESTD"));
    specs.push_back(zwm_spec<hdxf::ZwmChamferStdData>("ZWMCHAMFERSTD"));
    specs.push_back(zwm_spec<hdxf::ZwmCircleMarkStdData>("ZWMCIRCLEMARKSTD"));
    specs.push_back(zwm_spec<hdxf::ZwmColorStdData>("ZWMCOLORSTD"));
    specs.push_back(zwm_spec<hdxf::ZwmDatumIdStdData>("ZWMDATUMIDSTD"));
    specs.push_back(zwm_spec<hdxf::ZwmDatumTargetStdData>("ZWMDATUMTARGETSTD"));
    specs.push_back(zwm_spec<hdxf::ZwmDetailStdData>("ZWMDETAILSTD"));
    specs.push_back(zwm_spec<hdxf::ZwmDimStyleStdData>("ZWMDIMSTYLESTD"));
    specs.push_back(zwm_spec<hdxf::ZwmElevSymStdData>("ZWMELEVSYMSTD"));
    specs.push_back(zwm_spec<hdxf::ZwmFCFrameStdData>("ZWMFCFRAMESTD"));
    specs.push_back(zwm_spec<hdxf::ZwmFeatureIdStdData>("ZWMFEATUREIDSTD"));
    specs.push_back(zwm_spec<hdxf::ZwmFrameStyleStdData>("ZWMFRAMESTYLESTD"));
    specs.push_back(zwm_spec<hdxf::ZwmHoleChartStdData>("ZWMHOLECHARTSTD"));
    specs.push_back(zwm_spec<hdxf::ZwmLayerStdData>("ZWMLAYERSTD"));
    specs.push_back(zwm_spec<hdxf::ZwmLineTypeStdData>("ZWMLINETYPESTD"));
    specs.push_back(zwm_spec<hdxf::ZwmNoteStdData>("ZWMNOTESTD"));
    specs.push_back(zwm_spec<hdxf::ZwmPartRefStdData>("ZWMPARTREFSTD"));
    specs.push_back(zwm_spec<hdxf::ZwmSectionSymStdData>("ZWMSECTIONSYMSTD"));
    specs.push_back(zwm_spec<hdxf::ZwmStandardData>("ZWMSTANDARD"));
    specs.push_back(zwm_spec<hdxf::ZwmSurfSymStdData>("ZWMSURFSYMSTD"));
    specs.push_back(zwm_spec<hdxf::ZwmTaperSlopeStdData>("ZWMTAPERSLOPESTD"));
    specs.push_back(zwm_spec<hdxf::ZwmTextStyleStdData>("ZWMTEXTSTYLESTD"));
    specs.push_back(zwm_spec<hdxf::ZwmTitleStyleStdData>("ZWMTITLESTYLESTD"));
    specs.push_back(zwm_spec<hdxf::ZwmViewDirectionStdData>("ZWMVIEWDIRECTIONSTD"));
    specs.push_back(zwm_spec<hdxf::ZwmWeldingStdData>("ZWMWELDINGSTD"));
    assert_eq(28, static_cast<int>(specs.size()), "every ZWM type");
    int cells = 0;
    for (const ZwmSpec& spec : specs) {
        const ObjectCodec* codec = registry.find_object(spec.name);
        assert_true(codec != nullptr, spec.name);
        auto add = [&](Drawing& d) {
            Object o;
            o.type = codec->type;
            o.owner = "0";
            spec.fill(o);
            d.add_line({0, 0, 0}, {1, 1, 0});
            d.objects().push_back(o);
        };
        auto verify = [&](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            const Object* o = find(r, codec->type);
            return o ? spec.check(*o) : std::string("the record");
        };
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix(spec.name, codec->versions, add, verify);
        assert_eq(12, report.generated, std::string(spec.name) + ": 2000 ... 2018");
        assert_eq(2, report.named_errors, std::string(spec.name) + ": R12");
        assert_eq(4, report.not_established, std::string(spec.name) + ": R13, R14");
        cells += report.generated;
    }
    std::cout << "  28 ZWM types: " << cells << " cells generated, read back, written unchanged and converted; R12 named errors, R13/R14 not established; "
              << "AutoCAD verified 0, ej testat (F9.25)\n";
}

// ---------------------------------------------------------------------------
// TurboCAD
// ---------------------------------------------------------------------------

namespace {

int count_records(const std::vector<uint8_t>& ascii, const std::string& type) {
    int n = 0;
    for (const Rec& r : ascii_records(ascii)) if (!r.empty() && r[0].first == 0 && r[0].second == type) ++n;
    return n;
}

} // namespace

TEST(tc_zwm_turbocads_optional_data_of_five_files_is_typed_and_written_unchanged) {
    // AC1014 (builder f and w), AC1018 (version 1), AC1021 (the most forms)
    for (const char* file : {"tc_builder_plans_f.dxf", "tc_builder_plans_w.dxf", "tc_safecast_pj031ch.dxf", "tc_aphenphos_drawing1.dxf",
                             "tc_echocal_blockdiag.dxf"}) {
        std::vector<uint8_t> in = local_bytes(file);
        Drawing d = load(in, file);
        int typed = 0;
        for (const Object& o : d.objects()) if (o.type == ObjectType::TcOptionalData) ++typed;
        int expected = count_records(in, "TCOPTIONALDATA");
        assert_true(expected > 0, std::string(file) + ": records");
        assert_eq(expected, typed, std::string(file) + ": every record typed");
        assert_false(warned(d, "kept as they were read"), std::string(file) + ": nothing left untyped");
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, file);
        std::vector<Rec> want = records_of(in, "TCOPTIONALDATA"), got = records_of(out, "TCOPTIONALDATA");
        assert_eq(static_cast<int>(want.size()), static_cast<int>(got.size()), file);
        for (size_t i = 0; i < want.size(); ++i) assert_true(want[i] == got[i], std::string(file) + ": record " + std::to_string(i) + " unchanged is the file's");
        // the typed values alone
        for (Object& o : d.objects()) if (o.type == ObjectType::TcOptionalData) o.raw = hdxf::RawRecord();
        std::vector<uint8_t> alone = save(d, DxfFormat::Ascii, std::string(file) + " alone");
        got = records_of(alone, "TCOPTIONALDATA");
        assert_eq(static_cast<int>(want.size()), static_cast<int>(got.size()));
        for (size_t i = 0; i < want.size(); ++i) {
            std::string why = differ(want[i], got[i]);
            assert_true(why.empty(), std::string(file) + " record " + std::to_string(i) + ": " + why);
        }
    }
}

TEST(tc_zwm_optional_data_forms_are_read_by_their_flags) {
    std::vector<uint8_t> in = local_bytes("tc_echocal_blockdiag.dxf");
    Drawing d = load(in, "echocal");
    bool has_first_text_point = false, has_text_point = false, has_point_font = false, has_props_font = false, has_two_props = false, has_props_only = false;
    for (const Object& o : d.objects()) {
        if (o.type != ObjectType::TcOptionalData) continue;
        const hdxf::TcOptionalDataData& t = o.as_tc_optional_data();
        assert_eq(2, static_cast<int>(t.version));
        assert_eq(0, static_cast<int>(t.other_flags), "no unknown bits in this file");
        if (t.text && t.point && !t.properties) {
            has_text_point = true;
            assert_eq(std::string("Line/Polyline"), *t.text);
            assert_true(t.point->z == 0.0, "30");
            if (t.point->x == 410.0 && t.point->y == 79.25) has_first_text_point = true;
        }
        if (t.point && t.font && !t.properties) {
            has_point_font = true;
            assert_eq(std::string("Arial"), t.font->name);
        }
        if (t.properties && t.font) has_props_font = true;
        if (t.properties && t.properties->size() == 2) {
            has_two_props = true;
            assert_eq(std::string("#$AUX@_ToolInfo Properties"), (*t.properties)[0].name);
            assert_eq(8, static_cast<int>((*t.properties)[0].type));
        }
        if (t.properties && !t.font && !t.point && !t.text && !t.real) has_props_only = true;
    }
    assert_true(has_first_text_point && has_text_point && has_point_font && has_props_font && has_two_props && has_props_only, "five of the file's forms");
    // version 1 (AC1018): a string property is group 1
    Drawing e = load(local_bytes("tc_safecast_pj031ch.dxf"), "safecast");
    const hdxf::TcOptionalDataData& v1 = find(e, ObjectType::TcOptionalData)->as_tc_optional_data();
    assert_eq(1, static_cast<int>(v1.version));
    assert_eq(std::string("CMD_RECTANGLE@"), (*v1.properties)[0].text);
    // reals and integers (AC1014)
    Drawing f = load(local_bytes("tc_builder_plans_f.dxf"), "builder f");
    bool real_prop = false;
    for (const Object& o : f.objects()) {
        if (o.type != ObjectType::TcOptionalData || !o.as_tc_optional_data().properties) continue;
        for (const hdxf::TcProperty& p : *o.as_tc_optional_data().properties) {
            if (p.type == 5 && p.name == "#$AUX@_RENDER_ACIS_CUSTOMREF_NORMTOL") { real_prop = true; assert_true(p.real == 15.0, "40"); }
            if (p.type == 18) assert_eq(0, static_cast<int>(p.integer));
        }
    }
    assert_true(real_prop, "a real property");
}

TEST(tc_zwm_optional_data_changed_and_unknown_branches) {
    std::vector<uint8_t> in = local_bytes("tc_echocal_blockdiag.dxf");
    Drawing d = load(in, "echocal");
    // change a point, a font, the properties and add flag 32 on one record
    Object* target = nullptr;
    for (Object& o : d.objects()) if (o.type == ObjectType::TcOptionalData && o.as_tc_optional_data().point && o.as_tc_optional_data().font) target = &o;
    hdxf::TcOptionalDataData& t = target->as_tc_optional_data();
    t.point->x = 12.5;
    t.font->name = "Courier \xC3\xA5";
    t.properties = std::vector<hdxf::TcProperty>{};
    hdxf::TcProperty p;
    p.name = "Mine";
    p.type = 5;
    p.real = 0.75;
    t.properties->push_back(p);
    p.name = "Blob";
    p.type = 8;
    p.blob = {1, 2, 3, 250};
    t.properties->push_back(p);
    t.real = 4.0;
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "changed");
    Drawing back = load(out, "changed back");
    int found = 0;
    for (const Object& o : back.objects()) {
        if (o.type != ObjectType::TcOptionalData) continue;
        const hdxf::TcOptionalDataData& b = o.as_tc_optional_data();
        if (!b.font || b.font->name != "Courier \xC3\xA5") continue;
        ++found;
        assert_true(b.point->x == 12.5, "the point");
        assert_eq(2, static_cast<int>(b.properties->size()));
        assert_true((*b.properties)[0].real == 0.75, "the real");
        assert_eq(4, static_cast<int>((*b.properties)[1].blob.size()));
        assert_true(b.real && *b.real == 4.0, "flag 32 is new");
    }
    assert_eq(1, found, "the changed record");
    assert_true(save(back, DxfFormat::Ascii, "again") == out, "written again unchanged");
    // the other records are the file's
    std::vector<Rec> was = records_of(in, "TCOPTIONALDATA"), now = records_of(out, "TCOPTIONALDATA");
    int different = 0;
    for (size_t i = 0; i < was.size(); ++i) if (was[i] != now[i]) ++different;
    assert_eq(1, different, "one record differs");

    // an unknown property type is left untyped and preserved with a warning
    std::vector<uint8_t> f = local_bytes("tc_builder_plans_f.dxf");
    std::string text(f.begin(), f.end());
    size_t at = text.find("#$AUX@_RENDER_ACIS_REFINEMENT_MODE");
    assert_true(at != std::string::npos, "the property");
    size_t type_at = text.find("    18", at);
    assert_true(type_at != std::string::npos && type_at - at < 80, "its type 18");
    text.replace(type_at, 6, "    99");
    std::vector<uint8_t> bytes(text.begin(), text.end());
    Drawing e;
    assert_true(e.load_from_memory(bytes.data(), bytes.size()), e.last_error());
    assert_true(warned(e, "TCOPTIONALDATA"), "a warning names the record");
    assert_true(e.save_to_memory(DxfFormat::Ascii) == bytes, "kept byte for byte");
}

TEST(tc_zwm_new_optional_data_in_every_version_and_format) {
    const TypedRegistry& registry = TypedRegistry::instance();
    for (int version : {1, 2}) {
        auto add = [version](Drawing& d) {
            Object o;
            o.type = ObjectType::TcOptionalData;
            o.owner = "0";
            hdxf::TcOptionalDataData t;
            t.version = static_cast<int16_t>(version);
            t.text = "Line";
            t.point = hdxf::Point3{1, 2, 3};
            hdxf::TcProperty a;
            a.name = "A";
            a.type = 8;
            a.text = "text";
            a.blob = {0x41, 0x42};
            hdxf::TcProperty b;
            b.name = "B";
            b.type = 18;
            b.integer = 7;
            hdxf::TcProperty c;
            c.name = "C";
            c.type = 5;
            c.real = 1.5;
            t.properties = std::vector<hdxf::TcProperty>{a, b, c};
            t.font = hdxf::TcFont{"Arial", 0};
            t.real = 0.25;
            o.data = hdxf::Boxed<hdxf::TcOptionalDataData>(t);
            d.add_line({0, 0, 0}, {1, 1, 0});
            d.objects().push_back(o);
        };
        auto verify = [version](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            const Object* o = find(r, ObjectType::TcOptionalData);
            if (!o) return "the record";
            const hdxf::TcOptionalDataData& t = o->as_tc_optional_data();
            if (t.version != version || !t.text || *t.text != "Line" || !t.point || t.point->z != 3.0) return "the head";
            if (!t.properties || t.properties->size() != 3) return "the properties";
            const hdxf::TcProperty& a = (*t.properties)[0];
            if (version == 1 ? a.text != "text" : a.blob != std::vector<uint8_t>{0x41, 0x42}) return "the string property";
            if ((*t.properties)[1].integer != 7 || (*t.properties)[2].real != 1.5) return "the integer and the real";
            if (!t.font || t.font->name != "Arial" || !t.real || *t.real != 0.25) return "the font and the real";
            return {};
        };
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("TCOPTIONALDATA", registry.find_object("TCOPTIONALDATA")->versions, add, verify);
        assert_eq(14, report.generated, "R14 and 2000 ... 2018");
        assert_eq(2, report.named_errors, "R12");
        assert_eq(2, report.not_established, "R13");
        std::cout << "  TCOPTIONALDATA version " << version << ": " << report.generated << " cells generated, " << report.named_errors
                  << " named errors, not established " << report.not_established << ", AutoCAD verified " << report.autocad_verified
                  << ", ej testat " << report.autocad_not_tested << ", R13/R14 not savable " << report.autocad_not_savable << "\n";
    }
}

namespace {

// TurboCAD's six points of the echocal drawing are in one of its blocks, none in ENTITIES
std::vector<Entity*> tc_points(Drawing& d) {
    std::vector<Entity*> out;
    for (Entity& e : d.entities()) if (e.type == EntityType::TcPoint) out.push_back(&e);
    for (hdxf::Block& b : d.blocks()) for (Entity& e : b.entities) if (e.type == EntityType::TcPoint) out.push_back(&e);
    return out;
}

} // namespace

TEST(tc_zwm_the_tc_point_of_the_echocal_drawing_has_its_symbol_as_extents) {
    std::vector<uint8_t> in = local_bytes("tc_echocal_blockdiag.dxf");
    Drawing d = load(in, "echocal");
    std::vector<Entity*> points = tc_points(d);
    assert_eq(6, static_cast<int>(points.size()), "six typed TCPOINTENTITY (all in a block)");
    const Entity* point = nullptr;
    for (const Entity* e : points) if (e->props.handle == "1FC") point = e;
    assert_true(point != nullptr, "the point 1FC");
    const hdxf::TcPointData& t = point->as_tc_point();
    assert_true(t.point.location.x == -4.5 && t.point.location.y == -9.0, "AcDbPoint");
    assert_eq(1, static_cast<int>(t.version), "70");
    assert_eq(4, static_cast<int>(t.flags), "71");
    assert_true(t.size == 2.0, "40");
    assert_true(t.proxy_graphics.size() > 2000, "the symbol is the proxy graphics: " + std::to_string(t.proxy_graphics.size()));
    assert_eq(std::string("Text"), point->props.layer);
    hdxf::Point3 lo, hi;
    assert_true(d.entity_extents(*point, lo, hi), "extents");
    assert_true(hi.x > lo.x && hi.y > lo.y, "the symbol has a size");
    assert_true(lo.x <= t.point.location.x + 1e-6 && t.point.location.x <= hi.x + 1e-6, "it contains the point");
    // B12
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "unchanged");
    std::vector<Rec> want = records_of(in, "TCPOINTENTITY"), got = records_of(out, "TCPOINTENTITY");
    assert_eq(6, static_cast<int>(want.size()));
    assert_eq(6, static_cast<int>(got.size()));
    for (size_t i = 0; i < want.size(); ++i) assert_true(want[i] == got[i], "unchanged is the file's, graphics included");
    // the typed values alone
    for (Entity* e : tc_points(d)) e->raw = hdxf::RawRecord();
    std::vector<uint8_t> alone = save(d, DxfFormat::Ascii, "alone");
    got = records_of(alone, "TCPOINTENTITY");
    assert_eq(6, static_cast<int>(got.size()));
    for (size_t i = 0; i < want.size(); ++i) {
        std::string why = differ(want[i], got[i]);
        assert_true(why.empty(), "typed alone: " + why);
    }
    // a changed point and size
    Drawing e2 = load(in, "echocal");
    for (Entity* e : tc_points(e2)) {
        e->as_tc_point().point.location = {1.0, 2.0, 0.0};
        e->as_tc_point().size = 3.5;
    }
    Drawing back = load(save(e2, DxfFormat::Ascii, "changed"), "changed back");
    std::vector<Entity*> changed = tc_points(back);
    assert_eq(6, static_cast<int>(changed.size()));
    for (const Entity* e : changed) {
        assert_true(e->as_tc_point().point.location.x == 1.0 && e->as_tc_point().size == 3.5, "the change");
        if (e->props.handle == "1FC") assert_true(e->as_tc_point().proxy_graphics == t.proxy_graphics, "the symbol is kept");
    }
}

TEST(tc_zwm_a_new_tc_point_counts_its_graphics_in_every_version_and_format) {
    const TypedRegistry& registry = TypedRegistry::instance();
    hdxf::internal::ProxyGraphics g;
    g.polyline({{0, 0, 0}, {10, 5, 0}, {10, -5, 0}});
    const std::vector<uint8_t> graphics = g.bytes();
    auto add = [&](Drawing& d) {
        Entity e;
        e.type = EntityType::TcPoint;
        e.props.layer = "0";
        hdxf::TcPointData t;
        t.point.location = {2.0, 3.0, 0.0};
        t.size = 6.0;
        t.proxy_graphics = graphics;
        e.data = hdxf::Boxed<hdxf::TcPointData>(t);
        d.entities().push_back(e);
    };
    auto verify = [&](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
        const Entity* point = nullptr;
        for (const Entity& e : r.entities()) if (e.type == EntityType::TcPoint) point = &e;
        if (!point) return "the entity";
        const hdxf::TcPointData& t = point->as_tc_point();
        if (t.point.location.x != 2.0 || t.size != 6.0 || t.flags != 4) return "the fields";
        if (t.proxy_graphics != graphics) return "the symbol";
        hdxf::Point3 lo, hi;
        if (!r.entity_extents(*point, lo, hi) || lo.x != 0.0 || hi.x != 10.0 || lo.y != -5.0 || hi.y != 5.0) return "the symbol's extents";
        return {};
    };
    hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("TCPOINTENTITY", registry.find_entity("TCPOINTENTITY")->versions, add, verify);
    assert_eq(12, report.generated, "2000 ... 2018");
    assert_eq(2, report.named_errors, "R12");
    assert_eq(4, report.not_established, "R13, R14");
    std::cout << "  TCPOINTENTITY: " << report.generated << " cells generated, " << report.named_errors << " named errors, not established "
              << report.not_established << ", AutoCAD verified " << report.autocad_verified << ", ej testat " << report.autocad_not_tested << "\n";
}
