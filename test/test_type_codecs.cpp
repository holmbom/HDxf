#include "test_framework.hpp"
#include "type_version_matrix.hpp"
#include "typed_corpus.hpp"
#include "hdxf.hpp"
#include "TypedRecordTypes.hpp"
#include "dxf_writer.hpp"
#include "group_io.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The foundation of phase 9 (step 9.0, plan 2026-10-27-fas-9-typer.md): the record splitter and the group
// cursor, the registry, the codec boundary of the reader and the writer, B12 for typed records, the
// relation descriptors (B19, B104), the version matrix (B101), the EvalExpr value contract and the memory
// budget.
//
// The codec boundary is proved with probe codecs registered around a check on types that already have
// typed data (LIGHT and GROUP): what a codec reads and writes must come out exactly as the code that is
// there. Nothing of the families' own is in this file.
// ---------------------------------------------------------------------------

using hdxf::DxfFormat;
using hdxf::DxfGroup;
using hdxf::DxfVersion;
using hdxf::Point2;
using hdxf::Point3;
using namespace hdxf::internal;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

namespace {

std::vector<DxfGroup> groups(std::initializer_list<std::pair<int, const char*>> list) {
    std::vector<DxfGroup> out;
    for (const auto& [code, value] : list) out.push_back({code, value, 0});
    return out;
}

RecordView view_of(std::initializer_list<std::pair<int, const char*>> list) {
    RecordView view;
    std::string error;
    bool ok = RecordView::parse(groups(list), view, error);
    assert_true(ok, "RecordView::parse: " + error);
    return view;
}

// ---------------------------------------------------------------------------
// Probe codecs
// ---------------------------------------------------------------------------

int g_light_reads = 0;
int g_light_writes = 0;
int g_group_reads = 0;
int g_group_writes = 0;

bool probe_light_read(const RecordView& view, TypedReadContext& context, hdxf::Entity& e) {
    ++g_light_reads;
    GroupCursor c(view, "AcDbLight");
    if (!c.valid()) return context.fail("LIGHT has no AcDbLight subclass");
    hdxf::LightData d;
    c.require_integer(90, d.version);
    c.require_string(1, d.name);
    c.require_integer(70, d.light_type);
    c.require_boolean(290, d.on);
    c.require_integer(63, d.color.aci);
    c.integer(421, d.color.rgb);                // only when the color has a true color
    c.require_boolean(291, d.plot_glyph);
    c.require_real(40, d.intensity);
    c.require_point3(10, d.position);
    c.require_point3(11, d.target);
    c.require_integer(72, d.attenuation_type);
    c.require_boolean(292, d.use_limits);
    c.require_real(41, d.start_limit);
    c.require_real(42, d.end_limit);
    c.require_real(50, d.hotspot_angle);
    c.require_real(51, d.falloff_angle);
    c.require_boolean(293, d.cast_shadows);
    c.require_integer(73, d.shadow_type);
    c.require_integer(91, d.shadow_map_size);
    c.require_integer(280, d.shadow_softness);
    if (!c.ok()) return context.fail(c.error());
    e.data = hdxf::Boxed<hdxf::LightData>(d);
    return true;
}

// 2018 has not the list ACAD with AcDbSavedByObjectVersion alone on a LIGHT (as the writer does)
std::vector<hdxf::XdataList> without_saved_by(const std::vector<hdxf::XdataList>& xdata) {
    std::vector<hdxf::XdataList> out;
    for (const auto& x : xdata) {
        bool saved_by = x.app_name == "ACAD" && x.items.size() == 2 && x.items[0].code == 1000 &&
                        x.items[0].value == "AcDbSavedByObjectVersion" && x.items[1].code == 1070;
        if (!saved_by) out.push_back(x);
    }
    return out;
}

void probe_light_write(RecordWriter& w, const hdxf::Entity& e, const std::string& owner) {
    ++g_light_writes;
    const hdxf::LightData& d = e.as_light();
    std::vector<uint8_t> proxy;
    if (w.proxy_graphics_enabled()) proxy = {8, 0, 0, 0, 0, 0, 0, 0};
    w.entity_head("LIGHT", e, owner, &proxy);
    w.subclass("AcDbLight");
    w.integer(90, d.version);
    w.text(1, d.name);
    w.integer(70, d.light_type);
    w.boolean(290, d.on);
    w.integer(63, d.color.aci);
    if (d.color.has_rgb()) w.integer(421, d.color.rgb);
    w.boolean(291, d.plot_glyph);
    w.real(40, d.intensity);
    w.point3(10, d.position);
    w.point3(11, d.target);
    w.integer(72, d.attenuation_type);
    w.boolean(292, d.use_limits);
    w.real(41, d.start_limit);
    w.real(42, d.end_limit);
    w.real(50, d.hotspot_angle);
    w.real(51, d.falloff_angle);
    w.boolean(293, d.cast_shadows);
    w.integer(73, d.shadow_type);
    w.integer(91, d.shadow_map_size);
    w.integer(280, d.shadow_softness);
    w.xdata(w.version() >= DxfVersion::R2018 ? without_saved_by(e.props.xdata) : e.props.xdata);
}

bool g_probe_extents = false;

void probe_light_extents(const hdxf::Entity& e, TypedExtentsSink& sink) {
    if (g_probe_extents) sink.add_wcs(e.as_light().position);
}

EntityCodec light_codec() {
    EntityCodec c;
    c.dxf_name = "LIGHT";
    c.type = hdxf::EntityType::Light;
    c.read = probe_light_read;
    c.write = probe_light_write;
    c.extents = probe_light_extents;
    c.versions = VersionMatrix::native_from(DxfVersion::R2007);
    c.step = 0;
    return c;
}

bool probe_group_read(const RecordView& view, TypedReadContext& context, hdxf::Object& o) {
    ++g_group_reads;
    GroupCursor c(view, "AcDbGroup");
    if (!c.valid()) return context.fail("GROUP has no AcDbGroup subclass");
    hdxf::GroupData g;
    c.string(300, g.description);
    c.boolean(70, g.unnamed);
    c.boolean(71, g.selectable);
    while (c.code() == 340) {
        std::string handle;
        c.handle(340, handle);
        g.entities.push_back(handle);
    }
    if (!c.ok()) return context.fail(c.error());
    o.data = std::move(g);
    return true;
}

void probe_group_write(RecordWriter& w, const hdxf::Object& o) {
    ++g_group_writes;
    const hdxf::GroupData& g = o.as_group();
    w.object_head("GROUP", o);
    w.subclass("AcDbGroup");
    w.text(300, g.description);
    w.integer(70, g.unnamed ? 1 : 0);
    w.integer(71, g.selectable ? 1 : 0);
    for (const std::string& h : g.entities) w.pointer(340, h);
    w.xdata(o.xdata);
}

void probe_group_references(const hdxf::Object& o, std::vector<TypedReference>& out) {
    for (const std::string& h : o.as_group().entities) out.push_back({h, RefKind::HardPointer, true});
}

ObjectCodec group_codec(bool with_class = false) {
    ObjectCodec c;
    c.dxf_name = "GROUP";
    c.type = hdxf::ObjectType::Group;
    c.read = probe_group_read;
    c.write = probe_group_write;
    c.references = probe_group_references;
    c.versions = VersionMatrix::native_from(DxfVersion::R13);
    RefDescriptor members;
    members.subclass = "AcDbGroup";
    members.code = 340;
    members.kind = RefKind::HardPointer;
    members.removal = RefRemoval::DropMember;
    members.back_reactor = true;
    c.refs.push_back(members);
    ListSpec list;
    list.subclass = "AcDbGroup";
    list.codes = {340};
    c.lists.push_back(list);
    if (with_class) {
        c.cls.cpp_name = "AcDbGroup";
        c.cls.application = "ObjectDBX Classes";
        c.cls.proxy_flags = 0;
        c.cls.in_older = true;
        c.cls.older_application = "\"AutoCAD\"";
    }
    return c;
}

// The probe codecs in the registry while the object lives
struct ScopedProbes {
    bool light = false;
    bool group = false;
    // Since step 9.11 LIGHT has a codec of its own (sun_study.cpp): the probe replaces it for the test and it is put back
    std::optional<EntityCodec> light_before;
    std::optional<ObjectCodec> group_before;
    explicit ScopedProbes(bool with_light, bool with_group, bool group_class = false) {
        g_light_reads = g_light_writes = g_group_reads = g_group_writes = 0;
        TypedRegistry& registry = TypedRegistry::instance();
        if (with_light) {
            if (const EntityCodec* have = registry.find_entity("LIGHT")) light_before = *have;
            registry.add(light_codec());
            light = true;
        }
        if (with_group) {
            if (const ObjectCodec* have = registry.find_object("GROUP")) group_before = *have;
            registry.add(group_codec(group_class));
            group = true;
        }
    }
    ~ScopedProbes() {
        TypedRegistry& registry = TypedRegistry::instance();
        if (light) {
            registry.remove_entity("LIGHT");
            if (light_before) registry.add(*light_before);
        }
        if (group) {
            registry.remove_object("GROUP");
            if (group_before) registry.add(*group_before);
        }
    }
};

std::vector<uint8_t> read_bytes(const std::filesystem::path& path) { return hdxf_test::read_file_bytes(path); }

std::filesystem::path facit(const std::string& name) { return hdxf_test::source_path("doc/facit/" + name); }

bool same_light(const hdxf::LightData& a, const hdxf::LightData& b) {
    return a.version == b.version && a.name == b.name && a.light_type == b.light_type && a.on == b.on &&
           a.color.aci == b.color.aci && a.color.rgb == b.color.rgb && a.plot_glyph == b.plot_glyph &&
           a.intensity == b.intensity && a.position.x == b.position.x && a.position.y == b.position.y &&
           a.position.z == b.position.z && a.target.x == b.target.x && a.target.y == b.target.y &&
           a.target.z == b.target.z && a.attenuation_type == b.attenuation_type && a.use_limits == b.use_limits &&
           a.start_limit == b.start_limit && a.end_limit == b.end_limit && a.hotspot_angle == b.hotspot_angle &&
           a.falloff_angle == b.falloff_angle && a.cast_shadows == b.cast_shadows && a.shadow_type == b.shadow_type &&
           a.shadow_map_size == b.shadow_map_size && a.shadow_softness == b.shadow_softness;
}

std::vector<hdxf::Entity*> lights_of(hdxf::Drawing& d) {
    std::vector<hdxf::Entity*> out;
    for (auto& e : d.entities()) if (e.type == hdxf::EntityType::Light) out.push_back(&e);
    for (auto& b : d.blocks()) {
        for (auto& e : b.entities) if (e.type == hdxf::EntityType::Light) out.push_back(&e);
    }
    return out;
}

// A saved drawing's $VERSIONGUID is new at every save that changed something and $FINGERPRINTGUID in a new
// drawing: the same GUID in both
std::vector<uint8_t> without_version_guid(std::vector<uint8_t> bytes) {
    for (const std::string key : {"$VERSIONGUID", "$FINGERPRINTGUID"}) {
        auto at = std::search(bytes.begin(), bytes.end(), key.begin(), key.end());
        if (at == bytes.end()) continue;
        auto open = std::find(at, bytes.end(), static_cast<uint8_t>('{'));
        if (open == bytes.end() || bytes.end() - open < 38) continue;
        const std::string zeros = "00000000-0000-0000-0000-000000000000";
        std::copy(zeros.begin(), zeros.end(), open + 1);
    }
    return bytes;
}

std::string first_difference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    // HDXF_TEST_DUMP=<directory>: the two files, to compare them with a diff
    if (const char* dir = std::getenv("HDXF_TEST_DUMP")) {
        std::ofstream(std::filesystem::path(dir) / "expected.dxf", std::ios::binary).write(reinterpret_cast<const char*>(a.data()), static_cast<std::streamsize>(a.size()));
        std::ofstream(std::filesystem::path(dir) / "actual.dxf", std::ios::binary).write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    }
    size_t pos = 0;
    while (pos < a.size() && pos < b.size() && a[pos] == b[pos]) ++pos;
    size_t line = 1;
    for (size_t i = 0; i < pos; ++i) if (a[i] == '\n') ++line;
    return "first difference at byte " + std::to_string(pos) + " (line " + std::to_string(line) + "), " +
           std::to_string(a.size()) + " and " + std::to_string(b.size()) + " bytes";
}

} // namespace

// ---------------------------------------------------------------------------
// The record splitter
// ---------------------------------------------------------------------------

TEST(type_codecs_record_view_splits_head_subclasses_blocks_embedded_and_xdata) {
    RecordView v = view_of({
        {0, "PROBEOBJ"}, {5, "2A"},
        {102, "{ACAD_REACTORS"}, {330, "1F"}, {330, "20"}, {102, "}"},
        {102, "{ACAD_XDICTIONARY"}, {360, "3B"}, {102, "}"},
        {330, "1F"},
        {100, "AcDbFirst"}, {90, "7"}, {1, "text"},
        {102, "{APP_DATA"}, {40, "1.5"}, {102, "}"},
        {70, "3"},
        {100, "AcDbSecond"}, {90, "8"},
        {100, "AcDbSecond"}, {91, "5"},
        {101, "Embedded Object"}, {70, "1"}, {1, "inside"},
        {1001, "ACAD"}, {1000, "text"}, {1070, "5"},
    });
    assert_eq("PROBEOBJ", v.type_name());
    assert_eq("2A", v.head().handle);
    assert_eq("1F", v.head().owner);
    assert_eq("3B", v.head().xdictionary);
    assert_eq(2, static_cast<int>(v.head().reactors.size()));
    assert_eq("20", v.head().reactors[1]);
    assert_eq("AcDbFirst/AcDbSecond/AcDbSecond", v.chain());
    assert_eq(3, static_cast<int>(v.subclasses().size()));
    // the 102 block inside the first subclass is not among its groups: 90, 1 and 70
    assert_eq(3, static_cast<int>(v.subclasses()[0].indices.size()));
    assert_eq(1, static_cast<int>(v.blocks().size()));
    assert_eq("{APP_DATA", v.blocks()[0].name);
    assert_eq(0, static_cast<int>(v.blocks()[0].subclass));
    // the second subclass of the same name is occurrence 1
    assert_eq(2, static_cast<int>(v.subclass_count("AcDbSecond")));
    assert_eq(1, static_cast<int>(v.subclasses()[2].occurrence));
    assert_true(v.find_subclass("AcDbSecond", 1) == &v.subclasses()[2], "occurrence 1");
    assert_true(v.find_subclass("AcDbSecond", 2) == nullptr, "there is no occurrence 2");
    assert_true(v.find_subclass("AcDbMissing") == nullptr, "no such subclass");
    // the embedded object is neither a subclass group nor XDATA
    assert_eq(1, static_cast<int>(v.embedded().size()));
    assert_eq(3, static_cast<int>(v.embedded()[0].last - v.embedded()[0].first));
    assert_eq(1, static_cast<int>(v.subclasses()[2].indices.size()));
    assert_eq(1, static_cast<int>(v.xdata().size()));
    assert_eq("ACAD", v.xdata()[0].app_name);
    assert_eq(2, static_cast<int>(v.xdata()[0].items.size()));
    assert_eq(static_cast<int>(v.groups().size()) - 3, static_cast<int>(v.body_end()));

    // a record must start with its type, and a 102 group must close
    RecordView bad;
    std::string error;
    assert_false(RecordView::parse(groups({{5, "1"}}), bad, error), "no group 0");
    assert_false(RecordView::parse(groups({{0, "X"}, {5, "1"}, {100, "AcDbX"}, {102, "{OPEN"}, {40, "1"}}), bad, error),
                 "a 102 block that does not close");
    assert_true(error.find("is not closed") != std::string::npos, error);
}

TEST(type_codecs_class_data_before_the_first_1001_belongs_to_the_subclass) {
    // BLOCKLINEARPARAMETER's 1010 … 1071 stand in the subclass, directly before the XDATA (ACadSharp A81)
    RecordView v = view_of({
        {0, "BLOCKLIKE"}, {5, "2A"}, {330, "1F"},
        {100, "AcDbBlockElement"}, {300, "Distance1"}, {98, "33"}, {99, "327"}, {1071, "1"},
        {100, "AcDbBlockParameter"}, {280, "1"}, {281, "0"},
        {1010, "1.5"}, {1020, "2.5"}, {1030, "3.5"}, {1071, "9"},
        {1001, "ACAUTHENVIRON"}, {1000, "x"}, {1070, "1"},
    });
    assert_eq(1, static_cast<int>(v.xdata().size()));
    assert_eq("ACAUTHENVIRON", v.xdata()[0].app_name);
    GroupCursor c(v, "AcDbBlockElement");
    std::string name;
    int32_t version = 0, maintenance = 0, flags = 0;
    assert_true(c.string(300, name) && c.integer(98, version) && c.integer(99, maintenance) && c.integer(1071, flags), "element");
    assert_eq("Distance1", name);
    assert_eq(1, flags);
    GroupCursor p(v, "AcDbBlockParameter");
    int16_t a = 0, b = 0;
    Point3 point;
    int32_t extra = 0;
    assert_true(p.integer(280, a) && p.integer(281, b), "280, 281");
    assert_true(p.point3(1010, point), "class data as a point");
    assert_eq(2.5, point.y);
    assert_eq(3.5, point.z);
    assert_true(p.integer(1071, extra), "1071");
    assert_eq(9, extra);
    assert_true(p.at_end() && p.ok(), "the subclass is read to its end");

    // with no 1001 the class data at the end stays in the body, XDATA is empty
    RecordView w = view_of({{0, "X"}, {5, "1"}, {100, "AcDbA"}, {90, "1"}, {1010, "1.0"}, {1020, "2.0"}, {1030, "3.0"}});
    assert_eq(0, static_cast<int>(w.xdata().size()));
    assert_eq(4, static_cast<int>(w.subclasses()[0].indices.size()));
}

TEST(type_codecs_cursor_reads_in_order_with_ordinals_lists_and_rests) {
    RecordView v = view_of({
        {0, "X"}, {5, "1"},
        {100, "AcDbMap"}, {90, "2"}, {1, "a"}, {1, "b"}, {40, "0.5"}, {10, "1"}, {20, "2"}, {290, "1"}, {310, "0A0B"},
        {310, "FF"}, {91, "3"}, {340, "A1"}, {340, "A2"}, {340, "A3"},
    });
    GroupCursor c(v, "AcDbMap");
    assert_true(c.valid(), "valid");
    assert_eq("AcDbMap", c.name());
    int32_t n = 0;
    std::string s;
    assert_false(c.string(1, s), "the next group is 90, not 1");
    assert_true(c.ok(), "an absent group is no error");
    assert_true(c.integer(90, n), "90");
    assert_eq(2, n);
    assert_true(c.string(1, s) && s == "a", "first 1");
    assert_eq(1, static_cast<int>(c.ordinal(1)));
    assert_true(c.string(1, s) && s == "b", "second 1");
    assert_eq(2, static_cast<int>(c.ordinal(1)));
    double real = 0.0;
    assert_true(c.real(40, real) && real == 0.5, "40");
    Point2 p2;
    assert_true(c.point2(10, p2), "point2");
    assert_eq(2.0, p2.y);
    bool flag = false;
    assert_true(c.boolean(290, flag) && flag, "290");
    std::vector<uint8_t> bytes;
    assert_true(c.bytes(310, bytes), "310 chunks");
    assert_eq(3, static_cast<int>(bytes.size()));
    assert_eq(0xFF, static_cast<int>(bytes[2]));
    size_t count = 0;
    assert_true(c.count(91, 1, count), "a count the groups can hold");
    assert_eq(3, static_cast<int>(count));
    std::vector<std::string> handles;
    for (size_t i = 0; i < count; ++i) {
        std::string h;
        assert_true(c.handle(340, h), "340");
        handles.push_back(h);
    }
    assert_eq("A3", handles[2]);
    assert_true(c.at_end() && c.ok(), "all read");
    // what a codec does not read is taken as it is, to be kept (a future delversion's tail)
    GroupCursor d(v, "AcDbMap");
    d.skip(10);
    std::vector<DxfGroup> rest = d.take_rest();
    assert_eq(3, static_cast<int>(rest.size()));
    assert_eq(340, rest[0].code);
    assert_eq("A3", rest[2].value);
    assert_true(d.at_end(), "take_rest reads to the end");

    // an absent subclass is an empty cursor
    GroupCursor none(v, "AcDbNothing");
    assert_false(none.valid(), "invalid");
    assert_true(none.at_end(), "empty");
    assert_false(none.integer(90, n), "nothing to read");
}

TEST(type_codecs_cursor_errors_are_checked_before_anything_is_allocated) {
    {
        RecordView v = view_of({{0, "X"}, {5, "1"}, {100, "AcDbA"}, {90, "abc"}});
        GroupCursor c(v, "AcDbA");
        int32_t n = 0;
        assert_false(c.integer(90, n), "not an integer");
        assert_false(c.ok(), "error");
        assert_true(c.error().find("not an integer") != std::string::npos, c.error());
        assert_true(c.error().find("X") == 0, "the record's type leads the message: " + c.error());
    }
    {
        RecordView v = view_of({{0, "X"}, {5, "1"}, {100, "AcDbA"}, {70, "70000"}, {71, "-1"}});
        GroupCursor c(v, "AcDbA");
        int16_t narrow = 0;
        assert_false(c.integer(70, narrow), "70000 does not fit an int16");
        assert_true(c.error().find("does not fit 16 bits") != std::string::npos, c.error());
        GroupCursor d(v, "AcDbA");
        int32_t wide = 0;
        uint32_t unsigned_value = 0;
        assert_true(d.integer(70, wide) && wide == 70000, "but an int32");
        assert_false(d.integer(71, unsigned_value), "-1 is not an unsigned value");
    }
    {
        // 1 000 000 items cannot be in a record of four groups
        RecordView v = view_of({{0, "X"}, {5, "1"}, {100, "AcDbA"}, {90, "1000000"}, {1, "a"}, {1, "b"}, {1, "c"}});
        GroupCursor c(v, "AcDbA");
        size_t n = 0;
        assert_false(c.count(90, 1, n), "the count is more than the groups that remain");
        assert_true(c.error().find("more than") != std::string::npos, c.error());
        assert_eq(0, static_cast<int>(n));
        RecordView w = view_of({{0, "X"}, {5, "1"}, {100, "AcDbA"}, {90, "-3"}});
        GroupCursor d(w, "AcDbA");
        assert_false(d.count(90, 1, n), "a negative count");
        // two groups per item: 3 items need 6 groups
        RecordView x = view_of({{0, "X"}, {5, "1"}, {100, "AcDbA"}, {90, "3"}, {1, "a"}, {1, "b"}, {1, "c"}, {1, "d"}, {1, "e"}});
        GroupCursor e(x, "AcDbA");
        assert_false(e.count(90, 2, n), "3 items of 2 groups are not in 5 groups");
        GroupCursor f(x, "AcDbA");
        assert_true(f.count(90, 1, n) && n == 3, "3 items of 1 group are");
    }
    {
        RecordView v = view_of({{0, "X"}, {5, "1"}, {100, "AcDbA"}, {310, "ABC"}});
        GroupCursor c(v, "AcDbA");
        std::vector<uint8_t> bytes;
        assert_false(c.bytes(310, bytes), "an odd number of hex digits");
        assert_true(bytes.empty(), "nothing taken");
        RecordView w = view_of({{0, "X"}, {5, "1"}, {100, "AcDbA"}, {310, "0Z"}});
        GroupCursor d(w, "AcDbA");
        assert_false(d.bytes(310, bytes), "not a hex digit");
    }
    {
        RecordView v = view_of({{0, "X"}, {5, "1"}, {100, "AcDbA"}, {90, "1"}});
        GroupCursor c(v, "AcDbA");
        int16_t a = 0;
        assert_false(c.require_integer(70, a), "a group that must be there");
        assert_true(c.error().find("group 70 is missing") != std::string::npos, c.error());
        // the first error stays
        c.fail("another");
        assert_true(c.error().find("group 70 is missing") != std::string::npos, "the first failure is kept");
    }
    {
        RecordView v = view_of({{0, "X"}, {5, "1"}, {100, "AcDbA"}, {40, "1.5x"}, {41, ""}});
        GroupCursor c(v, "AcDbA");
        double d = 0.0;
        assert_false(c.real(40, d), "trailing text in a number");
        GroupCursor e(v, "AcDbA");
        e.skip(1);
        assert_false(e.real(41, d), "an empty number");
    }
    {
        // a long UTF-8 string is one value
        std::string long_text(5000, 'x');
        long_text += "\xE2\x88\x85";
        std::vector<DxfGroup> g = {{0, "X", 0}, {5, "1", 0}, {100, "AcDbA", 0}, {1, long_text, 0}};
        RecordView v;
        std::string error;
        assert_true(RecordView::parse(g, v, error), error);
        GroupCursor c(v, "AcDbA");
        std::string s;
        assert_true(c.string(1, s) && s == long_text, "long string");
    }
}

TEST(type_codecs_numbers_with_the_padding_of_ascii_and_signs_are_read) {
    RecordView v = view_of({{0, "X"}, {5, "1"}, {100, "AcDbA"}, {90, "        4"}, {40, "+1.5"}, {41, "1.797693134862314E+99"},
                            {290, "     1"}});
    GroupCursor c(v, "AcDbA");
    int32_t n = 0;
    double a = 0.0, b = 0.0;
    bool flag = false;
    assert_true(c.integer(90, n) && n == 4, "padded integer");
    assert_true(c.real(40, a) && a == 1.5, "signed real");
    assert_true(c.real(41, b) && b > 1.7e99 && b < 1.8e99, "AutoCAD's 1.797693134862314E+99");
    assert_true(c.boolean(290, flag) && flag, "padded bool");
}

// ---------------------------------------------------------------------------
// The registry
// ---------------------------------------------------------------------------

TEST(type_codecs_registry_registers_replaces_and_removes) {
    TypedRegistry& registry = TypedRegistry::instance();
    const size_t objects = registry.object_codecs().size();
    const size_t entities = registry.entity_codecs().size();

    ObjectCodec a;
    a.dxf_name = "HDXF_TEST_A";
    a.type = hdxf::ObjectType::MlineStyle;
    a.step = 3;
    registry.add(a);
    assert_eq(static_cast<int>(objects + 1), static_cast<int>(registry.object_codecs().size()));
    assert_true(registry.find_object("HDXF_TEST_A") != nullptr, "by name");
    assert_true(registry.find_object(hdxf::ObjectType::MlineStyle) != nullptr, "by type");
    assert_eq(3, registry.find_object("HDXF_TEST_A")->step);
    assert_true(registry.implemented("HDXF_TEST_A"), "implemented");
    assert_true(registry.find_object("HDXF_TEST_B") == nullptr, "an unregistered name");

    // a second codec for the same name replaces the first
    ObjectCodec a2 = a;
    a2.step = 4;
    registry.add(a2);
    assert_eq(static_cast<int>(objects + 1), static_cast<int>(registry.object_codecs().size()));
    assert_eq(4, registry.find_object("HDXF_TEST_A")->step);
    // … and so does a codec for the same type under another name
    ObjectCodec b;
    b.dxf_name = "HDXF_TEST_B";
    b.type = hdxf::ObjectType::MlineStyle;
    registry.add(b);
    assert_eq(static_cast<int>(objects + 1), static_cast<int>(registry.object_codecs().size()));
    assert_true(registry.find_object("HDXF_TEST_A") == nullptr, "A was replaced by B (one type, one codec)");
    assert_true(registry.find_object(hdxf::ObjectType::MlineStyle)->dxf_name == "HDXF_TEST_B", "B is the Scale codec");
    assert_true(registry.remove_object("HDXF_TEST_B"), "removed");
    assert_false(registry.remove_object("HDXF_TEST_B"), "removed once");
    assert_true(registry.find_object(hdxf::ObjectType::MlineStyle) == nullptr, "and the type is free again");
    assert_eq(static_cast<int>(objects), static_cast<int>(registry.object_codecs().size()));

    EntityCodec e;
    e.dxf_name = "HDXF_TEST_E";
    e.type = hdxf::EntityType::Shape;      // a type with no codec (LIGHT has one since step 9.11)
    registry.add(e);
    assert_true(registry.find_entity("HDXF_TEST_E") != nullptr && registry.find_entity(hdxf::EntityType::Shape) != nullptr,
                "entity by name and type");
    assert_true(registry.remove_entity("HDXF_TEST_E"), "entity removed");
    assert_eq(static_cast<int>(entities), static_cast<int>(registry.entity_codecs().size()));
}

TEST(type_codecs_registry_lists_every_inventoried_name_once) {
    const TypedRegistry& registry = TypedRegistry::instance();
    std::set<std::string> seen;
    for (const TypeRegistryEntry& e : registry.entries()) {
        assert_true(seen.insert(std::string(e.dxf_name)).second, "twice in the register: " + std::string(e.dxf_name));
        assert_true(registry.find_entry(e.dxf_name) == &e, "find_entry: " + std::string(e.dxf_name));
        assert_true(e.step >= 0 && e.step <= 18, "step: " + std::string(e.dxf_name));
        // a type with work left names its step; a typed type with none has step 0
        if (e.status == InventoryStatus::Pending || e.status == InventoryStatus::DerivedAlias || e.status == InventoryStatus::ZeroSamples) {
            assert_true(e.step >= 1, "no step for " + std::string(e.dxf_name));
        }
    }
    assert_true(registry.find_entry("LINE") && registry.find_entry("LINE")->status == InventoryStatus::AlreadyTyped, "LINE");
    assert_true(registry.find_entry("ACAD_TABLE") && registry.find_entry("ACAD_TABLE")->status == InventoryStatus::Pending &&
                    registry.find_entry("ACAD_TABLE")->kind == TypedKind::Entity && registry.find_entry("ACAD_TABLE")->step == 1,
                "ACAD_TABLE is 9.1's");
    assert_true(registry.find_entry("ZWMSTANDARD") && registry.find_entry("ZWMSTANDARD")->step == 18, "ZWMSTANDARD is 9.18's");
    assert_true(registry.find_entry("BLOCK") && registry.find_entry("BLOCK")->kind == TypedKind::Structural, "BLOCK");
    assert_true(registry.find_entry("PDFREFERENCE") && registry.find_entry("PDFREFERENCE")->status == InventoryStatus::DerivedAlias,
                "PDFREFERENCE is an alias");
    assert_true(registry.find_entry("NOT_A_TYPE") == nullptr, "an unknown name");
}

// ---------------------------------------------------------------------------
// Relation descriptors and lists (without a drawing)
// ---------------------------------------------------------------------------

TEST(type_codecs_reference_descriptors_find_by_subclass_occurrence_code_and_ordinal) {
    // SORTENTSTABLE's pair: built in, from the draw order's behaviour (B19)
    const RefDescriptor* pointer = find_reference("SORTENTSTABLE", "AcDbSortentsTable", 0, 331, 7);
    assert_true(pointer != nullptr, "331 in SORTENTSTABLE");
    assert_true(pointer->kind == RefKind::SoftPointer && pointer->removal == RefRemoval::DropPair, "an entity pointer, dropped with its pair");
    assert_eq(1, pointer->last_offset);
    const RefDescriptor* key = find_reference("SORTENTSTABLE", "AcDbSortentsTable", 0, 5, 0);
    assert_true(key != nullptr && key->kind == RefKind::SortKey && key->removal == RefRemoval::Keep, "5 is a sort key, never a pointer");
    assert_true(find_reference("SORTENTSTABLE", "AcDbOther", 0, 331, 0) == nullptr, "another subclass");
    assert_true(find_reference("LINE", "AcDbLine", 0, 331, 0) == nullptr, "a type without descriptors");

    TypedRegistry& registry = TypedRegistry::instance();
    TypeRelations rel;
    rel.dxf_name = "HDXF_REL";
    RefDescriptor first;
    first.subclass = "AcDbRel";
    first.occurrence = 1;
    first.code = 330;
    first.ordinal = 2;
    first.kind = RefKind::InternalId;
    first.removal = RefRemoval::Keep;
    RefDescriptor any;
    any.code = 340;
    any.kind = RefKind::HardOwner;
    rel.refs = {first, any};
    registry.add_relations(rel);
    assert_true(find_reference("HDXF_REL", "AcDbRel", 1, 330, 2) != nullptr, "occurrence 1, ordinal 2");
    assert_true(find_reference("HDXF_REL", "AcDbRel", 0, 330, 2) == nullptr, "occurrence 0 is not it");
    assert_true(find_reference("HDXF_REL", "AcDbRel", 1, 330, 1) == nullptr, "ordinal 1 is not it");
    assert_true(find_reference("HDXF_REL", "AnySubclass", 5, 340, 9) != nullptr, "no subclass, occurrence or ordinal given: every one");
    registry.remove_relations("HDXF_REL");
    assert_true(find_reference("HDXF_REL", "AcDbRel", 1, 330, 2) == nullptr, "removed");

    // pointer kinds
    assert_true(is_pointer_kind(RefKind::HardOwner) && is_pointer_kind(RefKind::SoftOwner) && is_pointer_kind(RefKind::HardPointer) &&
                    is_pointer_kind(RefKind::SoftPointer), "the pointer kinds");
    assert_false(is_pointer_kind(RefKind::SortKey) || is_pointer_kind(RefKind::InternalId) || is_pointer_kind(RefKind::ExternalText),
                 "sort keys, internal ids and handles held as text are not pointers");
}

TEST(type_codecs_list_specs_cover_codes_runs_and_whole_subclasses) {
    TypedRegistry& registry = TypedRegistry::instance();
    ListSpec codes;
    codes.subclass = "AcDbCodes";
    codes.id = 1;
    codes.codes = {90, 340};
    ListSpec second;
    second.subclass = "AcDbCodes";
    second.id = 2;
    second.codes = {91, 331};
    ListSpec run;
    run.subclass = "AcDbRun";
    run.shape = ListShape::FromCode;
    run.codes = {91};
    ListSpec whole;
    whole.subclass = "AcDbWhole";
    whole.occurrence = 1;
    whole.shape = ListShape::WholeSubclass;
    registry.add_lists("HDXF_LISTS", {codes, second, run, whole});

    bool started = false;
    assert_eq(1, find_list("HDXF_LISTS", "AcDbCodes", 0, 340, started));
    assert_eq(2, find_list("HDXF_LISTS", "AcDbCodes", 0, 331, started));
    assert_eq(0, find_list("HDXF_LISTS", "AcDbCodes", 0, 70, started));
    assert_eq(0, find_list("HDXF_LISTS", "AcDbOther", 0, 340, started));
    // a run starts at its first code and takes everything after it
    started = false;
    assert_eq(0, find_list("HDXF_LISTS", "AcDbRun", 0, 70, started));
    assert_eq(1, find_list("HDXF_LISTS", "AcDbRun", 0, 91, started));
    assert_true(started, "the run has started");
    assert_eq(1, find_list("HDXF_LISTS", "AcDbRun", 0, 70, started));
    // a whole subclass, but only its second occurrence
    assert_eq(0, find_list("HDXF_LISTS", "AcDbWhole", 0, 70, started));
    assert_eq(1, find_list("HDXF_LISTS", "AcDbWhole", 1, 70, started));
    registry.remove_lists("HDXF_LISTS");
    assert_eq(0, find_list("HDXF_LISTS", "AcDbCodes", 0, 340, started));
}

// ---------------------------------------------------------------------------
// The version and generation matrix
// ---------------------------------------------------------------------------

TEST(type_codecs_version_matrix_has_nine_versions_two_formats_and_three_axes) {
    VersionMatrix m = VersionMatrix::native_from(DxfVersion::R2007);
    for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
        for (DxfVersion v : {DxfVersion::R12, DxfVersion::R13, DxfVersion::R14, DxfVersion::R2000, DxfVersion::R2004}) {
            assert_true(m.at(v, f).generation == Generation::None && m.at(v, f).basis == ErrorBasis::Documented, "before 2007: a named error");
            assert_false(m.can_generate(v, f), "cannot generate before 2007");
        }
        for (DxfVersion v : {DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018}) {
            assert_true(m.at(v, f).generation == Generation::Native && m.at(v, f).basis == ErrorBasis::NotApplicable, "from 2007: native");
        }
    }
    // the AutoCAD axis is its own: nothing verified, and AutoCAD 2025 cannot save R13 and R14
    assert_true(m.at(DxfVersion::R2010, DxfFormat::Ascii).autocad == AutoCadStatus::NotTested, "ej testat");
    assert_true(m.at(DxfVersion::R13, DxfFormat::Binary).autocad == AutoCadStatus::NotSavable, "R13");
    assert_true(m.at(DxfVersion::R14, DxfFormat::Ascii).autocad == AutoCadStatus::NotSavable, "R14");
    m.autocad(DxfVersion::R2010, AutoCadStatus::Verified);
    assert_true(m.at(DxfVersion::R2010, DxfFormat::Binary).autocad == AutoCadStatus::Verified, "both formats");
    m.autocad(DxfVersion::R2013, DxfFormat::Binary, AutoCadStatus::Verified);
    assert_true(m.at(DxfVersion::R2013, DxfFormat::Binary).autocad == AutoCadStatus::Verified &&
                    m.at(DxfVersion::R2013, DxfFormat::Ascii).autocad == AutoCadStatus::NotTested, "one format");

    // the generation axis is independent of the AutoCAD status and of the error
    m.set(DxfVersion::R2004, Generation::OlderForm);
    assert_true(m.can_generate(DxfVersion::R2004, DxfFormat::Ascii) && m.can_generate(DxfVersion::R2004, DxfFormat::Binary), "an older form");
    assert_true(m.first_version() && *m.first_version() == DxfVersion::R2004, "the first version both formats can make");
    m.set(DxfVersion::R2018, DxfFormat::Binary, Generation::None, ErrorBasis::NotEstablished);
    assert_true(m.can_generate(DxfVersion::R2018, DxfFormat::Ascii) && !m.can_generate(DxfVersion::R2018, DxfFormat::Binary),
                "a cell of one format");
    assert_true(m.at(DxfVersion::R2018, DxfFormat::Binary).basis == ErrorBasis::NotEstablished, "no claim");
}

TEST(type_codecs_version_error_text_is_named_from_the_first_version) {
    assert_eq("X requires DXF 2010 or later", version_error_text("X", VersionMatrix::native_from(DxfVersion::R2010), DxfVersion::R2004));
    assert_eq("X requires DXF R14 or later", version_error_text("X", VersionMatrix::native_from(DxfVersion::R14), DxfVersion::R13));
    assert_eq("X requires DXF 2000 or later", version_error_text("X", VersionMatrix::native_from(DxfVersion::R2000), DxfVersion::R12));
    VersionMatrix m = VersionMatrix::native_from(DxfVersion::R2013);
    m.error(DxfVersion::R2007, "An X of this kind requires DXF 2013 or later");
    assert_eq("An X of this kind requires DXF 2013 or later", version_error_text("X", m, DxfVersion::R2007));
    assert_eq("X requires DXF 2013 or later", version_error_text("X", m, DxfVersion::R2010));
    // nothing established: no claim about a first version
    assert_eq("X is not available in DXF 2013", version_error_text("X", VersionMatrix::unestablished(), DxfVersion::R2013));
    assert_false(VersionMatrix::unestablished().first_version().has_value(), "no first version");
}

// ---------------------------------------------------------------------------
// EvalExpr, the value contract of the dynamic block and associative families
// ---------------------------------------------------------------------------

namespace {

std::vector<DxfGroup> written_groups(const std::function<void(RecordWriter&)>& write, DxfVersion version, DxfFormat format) {
    std::ostringstream out(std::ios::binary);
    // binary DXF starts with its sentinel; the reader takes the width of the codes from the first group, 0/SECTION
    if (format == DxfFormat::Binary) out << std::string("AutoCAD Binary DXF\r\n\x1a\0", 22);
    DxfWriter w(out, version, format);
    RecordWriter rw(w);
    rw.text(0, "SECTION");
    rw.text(2, "ENTITIES");
    write(rw);
    std::istringstream in(out.str(), std::ios::binary);
    GroupReader reader(in);
    reader.detect_format();
    std::vector<DxfGroup> g;
    GroupCode gc;
    while (reader.read(gc)) g.push_back({gc.code, gc.value, 0});
    assert_true(g.size() >= 2 && g[0].code == 0 && g[1].code == 2, "the two groups written first");
    g.erase(g.begin(), g.begin() + 2);
    return g;
}

} // namespace

TEST(type_codecs_eval_expr_value_contract_reads_and_writes_both_forms) {
    for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
        // the node alone (911 of the 1275 AcDbEvalExpr in the corpus)
        hdxf::EvalExprData plain;
        plain.node_id = 4;
        plain.version = 33;
        plain.maintenance = 327;
        std::vector<DxfGroup> g = written_groups([&](RecordWriter& w) { write_eval_expr(w, plain); }, DxfVersion::R2018, format);
        assert_eq(3, static_cast<int>(g.size()));
        assert_eq(90, g[0].code);
        assert_eq(99, g[2].code);
        std::vector<DxfGroup> record = {{0, "X", 0}, {5, "1", 0}, {100, "AcDbEvalExpr", 0}};
        record.insert(record.end(), g.begin(), g.end());
        RecordView v;
        std::string error;
        assert_true(RecordView::parse(record, v, error), error);
        GroupCursor c(v, "AcDbEvalExpr");
        hdxf::EvalExprData back;
        assert_true(read_eval_expr(c, back) && c.at_end(), "plain: " + c.error());
        assert_eq(4, back.node_id);
        assert_eq(33, back.version);
        assert_eq(327, back.maintenance);
        assert_false(back.expression.has_value(), "no expression");

        // 1, 70 = 40 and 140: BLOCKGRIPLOCATIONCOMPONENT's expression with a real value
        hdxf::EvalExprData real_valued = plain;
        real_valued.expression = hdxf::EvalExprValue{"", hdxf::EvalVariant{40, 1.5, 0}};
        g = written_groups([&](RecordWriter& w) { write_eval_expr(w, real_valued); }, DxfVersion::R2018, format);
        assert_eq(6, static_cast<int>(g.size()));
        assert_eq(1, g[3].code);
        assert_eq(70, g[4].code);
        assert_eq(140, g[5].code);
        record.resize(3);
        record.insert(record.end(), g.begin(), g.end());
        assert_true(RecordView::parse(record, v, error), error);
        GroupCursor d(v, "AcDbEvalExpr");
        assert_true(read_eval_expr(d, back), d.error());
        assert_true(back.expression.has_value(), "the expression");
        assert_eq(40, back.expression->value.tag);
        assert_eq(1.5, back.expression->value.real);

        // 1, 70 = 70 and a second 70: the 24 of the dynamic block facit
        hdxf::EvalExprData int_valued = plain;
        int_valued.expression = hdxf::EvalExprValue{"UpdatedBaseX", hdxf::EvalVariant{70, 0.0, 12}};
        g = written_groups([&](RecordWriter& w) { write_eval_expr(w, int_valued); }, DxfVersion::R2018, format);
        assert_eq(6, static_cast<int>(g.size()));
        assert_eq(70, g[5].code);
        record.resize(3);
        record.insert(record.end(), g.begin(), g.end());
        assert_true(RecordView::parse(record, v, error), error);
        GroupCursor e(v, "AcDbEvalExpr");
        assert_true(read_eval_expr(e, back), e.error());
        assert_eq("UpdatedBaseX", back.expression->expression);
        assert_eq(70, back.expression->value.tag);
        assert_eq(12, back.expression->value.integer);
    }

    // a value type no AutoCAD file has: the record is not typed (kept as it was read, with a warning)
    RecordView unknown = view_of({{0, "X"}, {5, "1"}, {100, "AcDbEvalExpr"}, {90, "1"}, {98, "33"}, {99, "327"}, {1, ""}, {70, "91"}, {91, "5"}});
    GroupCursor c(unknown, "AcDbEvalExpr");
    hdxf::EvalExprData out;
    assert_false(read_eval_expr(c, out), "an unestablished tag");
    assert_true(c.error().find("not established") != std::string::npos, c.error());
    // a node without its 98 is an error, not a default
    RecordView truncated = view_of({{0, "X"}, {5, "1"}, {100, "AcDbEvalExpr"}, {90, "1"}});
    GroupCursor d(truncated, "AcDbEvalExpr");
    assert_false(read_eval_expr(d, out), "truncated");
}

// ---------------------------------------------------------------------------
// The codec boundary: probe codecs on types that already have typed data
// ---------------------------------------------------------------------------

TEST(type_codecs_light_probe_reads_and_writes_as_the_code_that_is_there) {
    const char* files[] = {"acad_2007_dxf_objects.dxf",     "acad_2007_dxf_objects_bin.dxf",
                           "acad_2010_dxf_objects.dxf",     "acad_2010_dxf_objects_bin.dxf",
                           "acad_2013_dxf_objects.dxf",     "acad_2013_dxf_objects_bin.dxf",
                           "acad_2018_dxf_objects.dxf",     "acad_2018_dxf_objects_bin.dxf"};
    for (const char* name : files) {
        std::vector<uint8_t> in = read_bytes(facit(name));
        if (in.empty()) hdxf_test::skip(std::string("missing ") + name);

        hdxf::Drawing plain;
        assert_true(plain.load_from_memory(in.data(), in.size()), std::string(name) + ": " + plain.last_error());
        std::vector<hdxf::LightData> expected;
        for (hdxf::Entity* e : lights_of(plain)) expected.push_back(e->as_light());
        assert_true(!expected.empty(), std::string(name) + ": the AutoCAD file has lights");

        ScopedProbes probes(true, false);
        hdxf::Drawing typed;
        assert_true(typed.load_from_memory(in.data(), in.size()), std::string(name) + ": " + typed.last_error());
        std::vector<hdxf::Entity*> lights = lights_of(typed);
        assert_eq(static_cast<int>(expected.size()), static_cast<int>(lights.size()), std::string(name) + ": lights");
        assert_true(g_light_reads >= static_cast<int>(expected.size()), "the codec read them");
        for (size_t i = 0; i < lights.size(); ++i) {
            assert_true(same_light(expected[i], lights[i]->as_light()), std::string(name) + ": light " + std::to_string(i) + " as the reader reads it");
            assert_true(lights[i]->props.layer == "*ADSK_SYSTEM_LIGHTS", "the head: the layer");
            assert_false(lights[i]->props.xdictionary.empty(), "the head: the extension dictionary");
            assert_false(lights[i]->raw.empty(), "the preserved record");
        }

        // unchanged: the file again, byte for byte (B12)
        DxfFormat format = std::string(name).find("_bin") != std::string::npos ? DxfFormat::Binary : DxfFormat::Ascii;
        std::vector<uint8_t> out = typed.save_to_memory(format);
        assert_true(out == in, std::string(name) + ": unchanged, " + first_difference(in, out));
        assert_true(g_light_writes > 0, "the codec wrote them");
    }
}

TEST(type_codecs_light_probe_changes_are_written_as_the_code_that_is_there_writes_them) {
    const char* files[] = {"acad_2007_dxf_objects.dxf", "acad_2010_dxf_objects_bin.dxf", "acad_2013_dxf_objects.dxf",
                           "acad_2018_dxf_objects_bin.dxf"};
    for (const char* name : files) {
        std::vector<uint8_t> in = read_bytes(facit(name));
        if (in.empty()) hdxf_test::skip(std::string("missing ") + name);
        DxfFormat format = std::string(name).find("_bin") != std::string::npos ? DxfFormat::Binary : DxfFormat::Ascii;

        // scalar changed, optional removed, optional added, a string made longer
        auto change = [&](hdxf::Drawing& d, int scenario) {
            std::vector<hdxf::Entity*> lights = lights_of(d);
            assert_true(lights.size() >= 2, "two lights");
            hdxf::LightData& a = lights[0]->as_light();
            hdxf::LightData& b = lights[1]->as_light();
            switch (scenario) {
                case 0: a.intensity += 0.5; break;                                   // a scalar
                case 1: a.color.rgb = -1; break;                                     // an optional group gone (421)
                case 2: b.color.rgb = 0x123456; a.color.rgb = 0xABCDEF; break;       // an optional group made
                case 3: a.name += std::string(300, 'n') + "\xE2\x88\x85"; break;     // a long UTF-8 string
                case 4: a.position.z = -7.25; b.target.x = 3.5; break;               // points
                default: break;
            }
        };
        for (int scenario = 0; scenario < 5; ++scenario) {
            hdxf::Drawing plain;
            assert_true(plain.load_from_memory(in.data(), in.size()), plain.last_error());
            change(plain, scenario);
            std::vector<uint8_t> expected = without_version_guid(plain.save_to_memory(format));
            assert_true(!expected.empty() && expected != in, std::string(name) + ": the change is written (" + plain.last_error() + ")");

            ScopedProbes probes(true, false);
            hdxf::Drawing typed;
            assert_true(typed.load_from_memory(in.data(), in.size()), typed.last_error());
            change(typed, scenario);
            std::vector<uint8_t> out = without_version_guid(typed.save_to_memory(format));
            assert_true(out == expected, std::string(name) + " scenario " + std::to_string(scenario) + ": " + first_difference(expected, out));
        }
    }
}

TEST(type_codecs_a_record_with_its_raw_emptied_gives_the_same_model_and_a_new_valid_record) {
    const char* files[] = {"acad_2007_dxf_objects.dxf", "acad_2010_dxf_objects_bin.dxf", "acad_2013_dxf_objects.dxf",
                           "acad_2018_dxf_objects_bin.dxf"};
    for (const char* name : files) {
        std::vector<uint8_t> in = read_bytes(facit(name));
        if (in.empty()) hdxf_test::skip(std::string("missing ") + name);
        DxfFormat format = std::string(name).find("_bin") != std::string::npos ? DxfFormat::Binary : DxfFormat::Ascii;

        // the code that is there: the records written anew from their typed values
        hdxf::Drawing plain;
        assert_true(plain.load_from_memory(in.data(), in.size()), plain.last_error());
        for (hdxf::Entity* e : lights_of(plain)) e->raw = hdxf::RawRecord();
        for (auto& o : plain.objects()) if (o.type == hdxf::ObjectType::Group) o.raw = hdxf::RawRecord();
        std::vector<uint8_t> expected = without_version_guid(plain.save_to_memory(format));
        assert_true(!expected.empty(), plain.last_error());

        ScopedProbes probes(true, true);
        hdxf::Drawing typed;
        assert_true(typed.load_from_memory(in.data(), in.size()), typed.last_error());
        std::vector<hdxf::LightData> before;
        for (hdxf::Entity* e : lights_of(typed)) before.push_back(e->as_light());
        for (hdxf::Entity* e : lights_of(typed)) e->raw = hdxf::RawRecord();
        for (auto& o : typed.objects()) if (o.type == hdxf::ObjectType::Group) o.raw = hdxf::RawRecord();
        std::vector<uint8_t> out = typed.save_to_memory(format);
        assert_true(!out.empty(), typed.last_error());
        assert_true(without_version_guid(out) == expected, std::string(name) + ": the codec's new records: " + first_difference(expected, without_version_guid(out)));

        // the new file is valid and holds the same model
        hdxf::Drawing back;
        assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
        std::vector<hdxf::Entity*> lights = lights_of(back);
        assert_eq(static_cast<int>(before.size()), static_cast<int>(lights.size()), std::string(name) + ": lights");
        for (size_t i = 0; i < lights.size(); ++i) {
            assert_true(same_light(before[i], lights[i]->as_light()), std::string(name) + ": light " + std::to_string(i) + " is the same model");
        }
    }
}

TEST(type_codecs_light_probe_builds_a_record_without_a_template_and_its_extents) {
    for (DxfVersion version : {DxfVersion::R2007, DxfVersion::R2013, DxfVersion::R2018}) {
        for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
            auto build = [&](hdxf::Drawing& d) {
                hdxf::Entity e;
                e.type = hdxf::EntityType::Light;
                hdxf::LightData light;
                light.name = "BUILT";
                light.light_type = 3;
                light.intensity = 2.5;
                light.position = {10.0, 20.0, 30.0};
                light.color.rgb = 0x102030;
                e.data = hdxf::Boxed<hdxf::LightData>(light);
                e.props.layer = "0";
                d.add_entity(e);
            };
            hdxf::Drawing plain(version);
            build(plain);
            std::vector<uint8_t> expected = without_version_guid(plain.save_to_memory(format));
            assert_true(!expected.empty(), plain.last_error());

            ScopedProbes probes(true, false);
            hdxf::Drawing typed(version);
            build(typed);
            g_probe_extents = false;            // the drawing's extents are the default's while the bytes are compared
            std::vector<uint8_t> out = without_version_guid(typed.save_to_memory(format));
            assert_true(g_light_writes == 1, "written by the codec once");
            assert_true(out == expected, std::string(hdxf_test::version_label(version)) + " " + hdxf_test::format_label(format) +
                                             ": the new record as the code that is there writes it: " + first_difference(expected, out));

            // the codec's extents are what the drawing gives for the entity; none is the default
            g_probe_extents = true;
            Point3 lo, hi;
            hdxf::Drawing read;
            assert_true(read.load_from_memory(out.data(), out.size()), read.last_error());
            std::vector<hdxf::Entity*> lights = lights_of(read);
            assert_eq(1, static_cast<int>(lights.size()));
            assert_true(read.entity_extents(*lights[0], lo, hi), "extents from the codec");
            assert_eq(10.0, lo.x);
            assert_eq(30.0, hi.z);
            g_probe_extents = false;
        }
    }
}

TEST(type_codecs_light_probe_version_matrix_over_nine_versions_and_two_formats) {
    ScopedProbes probes(true, false);
    const EntityCodec* codec = TypedRegistry::instance().find_entity("LIGHT");
    assert_true(codec != nullptr, "registered");
    auto add = [](hdxf::Drawing& d) {
        hdxf::Entity e;
        e.type = hdxf::EntityType::Light;
        hdxf::LightData light;
        light.name = "MATRIX";
        light.position = {1.0, 2.0, 3.0};
        e.data = hdxf::Boxed<hdxf::LightData>(light);
        d.add_entity(e);
    };
    auto verify = [](const hdxf::Drawing& d, DxfVersion, DxfFormat) -> std::string {
        hdxf::Drawing& m = const_cast<hdxf::Drawing&>(d);
        std::vector<hdxf::Entity*> lights = lights_of(m);
        if (lights.size() != 1) return "the light is not there";
        if (lights[0]->as_light().name != "MATRIX") return "its name";
        return {};
    };
    hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("LIGHT", codec->versions, add, verify);
    // 2007, 2010, 2013, 2018 in two formats are generated; 2000, 2004 and R12, R13, R14 in two formats are named errors
    assert_eq(8, report.generated);
    assert_eq(10, report.named_errors);
    assert_eq(0, report.not_established);
    // the AutoCAD axis: nothing is claimed verified, everything else is reported
    assert_eq(0, report.autocad_verified);
    assert_eq(14, report.autocad_not_tested);
    assert_eq(4, report.autocad_not_savable);
    assert_eq(18, static_cast<int>(report.not_tested.size()));
}

TEST(type_codecs_group_probe_reads_writes_and_derives_reactors_as_the_code_that_is_there) {
    const char* files[] = {"acad_2000_dxf_objects.dxf",        "acad_2000_dxf_objects_bin.dxf",
                           "acad_2004_dxf_objects.dxf",        "acad_2007_dxf_objects_bin.dxf",
                           "acad_2010_dxf_objects.dxf",        "acad_2013_dxf_objects_bin.dxf",
                           "acad_2018_dxf_objects.dxf",        "acad_2018_dxf_objects_group_member_removed.dxf",
                           "acad_2018_dxf_objects_group_member_removed_bin.dxf"};
    for (const char* name : files) {
        std::vector<uint8_t> in = read_bytes(facit(name));
        if (in.empty()) hdxf_test::skip(std::string("missing ") + name);
        DxfFormat format = std::string(name).find("_bin") != std::string::npos ? DxfFormat::Binary : DxfFormat::Ascii;

        auto groups_of = [](hdxf::Drawing& d) {
            std::vector<hdxf::Object*> out;
            for (auto& o : d.objects()) if (o.type == hdxf::ObjectType::Group) out.push_back(&o);
            return out;
        };
        // scenarios: 0 nothing, 1 description, 2 first member removed, 3 a member added, 4 an entity of the
        // group erased from the drawing (the group loses the member, B19)
        auto change = [&](hdxf::Drawing& d, int scenario) {
            std::vector<hdxf::Object*> gs = groups_of(d);
            assert_true(!gs.empty(), "a GROUP");
            hdxf::GroupData& g = gs[0]->as_group();
            switch (scenario) {
                case 1: g.description = "changed"; break;
                case 2: if (!g.entities.empty()) g.entities.erase(g.entities.begin()); break;
                case 3: g.entities.push_back(d.entities().front().props.handle); break;
                case 4: {
                    std::string member = g.entities.empty() ? std::string() : g.entities.back();
                    auto& list = d.entities();
                    for (size_t i = 0; i < list.size(); ++i) {
                        if (list[i].props.handle == member) { list.erase(list.begin() + static_cast<long>(i)); break; }
                    }
                    break;
                }
                default: break;
            }
        };
        for (int scenario = 0; scenario < 5; ++scenario) {
            hdxf::Drawing plain;
            assert_true(plain.load_from_memory(in.data(), in.size()), plain.last_error());
            change(plain, scenario);
            std::vector<uint8_t> expected = plain.save_to_memory(format);
            assert_true(!expected.empty(), plain.last_error());
            if (scenario == 0) assert_true(expected == in, "unchanged");
            expected = without_version_guid(std::move(expected));

            ScopedProbes probes(false, true);
            hdxf::Drawing typed;
            assert_true(typed.load_from_memory(in.data(), in.size()), typed.last_error());
            assert_true(g_group_reads > 0, "read by the codec");
            change(typed, scenario);
            std::vector<uint8_t> out = without_version_guid(typed.save_to_memory(format));
            assert_true(out == expected, std::string(name) + " scenario " + std::to_string(scenario) + ": " + first_difference(expected, out));
            assert_true(g_group_writes > 0, "written by the codec");
        }
    }
}

TEST(type_codecs_group_probe_version_matrix_includes_r13_and_r14) {
    ScopedProbes probes(false, true);
    const ObjectCodec* codec = TypedRegistry::instance().find_object("GROUP");
    assert_true(codec != nullptr, "registered");
    auto add = [](hdxf::Drawing& d) {
        hdxf::Object o;
        o.type = hdxf::ObjectType::Group;
        o.handle = "FFF2";
        o.owner = "0";
        hdxf::GroupData g;
        g.description = "matrix group";
        g.selectable = false;
        o.data = g;
        d.objects().push_back(o);
    };
    auto verify = [](const hdxf::Drawing& d, DxfVersion, DxfFormat) -> std::string {
        const hdxf::Object* o = d.find_object("FFF2");
        if (!o || o->type != hdxf::ObjectType::Group) return "the GROUP is not there as a GROUP";
        if (o->as_group().description != "matrix group" || o->as_group().selectable) return "its fields";
        return {};
    };
    hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("GROUP", codec->versions, add, verify);
    // R13, R14, 2000, 2004, 2007, 2010, 2013, 2018 in two formats are generated (R13 and R14 through the target
    // profile, their objects read back by the codec); R12 has no OBJECTS: a named error
    assert_eq(16, report.generated);
    assert_eq(2, report.named_errors);
    assert_true(g_group_writes > 0 && g_group_reads > 0, "through the codec both ways");
}

TEST(type_codecs_codec_class_is_added_once_to_classes_and_follows_the_older_register) {
    for (DxfVersion version : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R14, DxfVersion::R13}) {
        auto build = [&]() {
            hdxf::Drawing d(version);
            hdxf::Object o;
            o.type = hdxf::ObjectType::Group;
            o.handle = "FFF1";
            o.owner = "0";
            hdxf::GroupData g;
            g.description = "probe";
            o.data = g;
            d.objects().push_back(o);
            return d;
        };
        // without a codec the GROUP has no class (it is a built-in type)
        {
            hdxf::Drawing d = build();
            std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
            assert_true(!bytes.empty(), d.last_error());
            std::string text = hdxf_test::lf_text(bytes);
            assert_true(text.find("\n  1\nGROUP\n") == std::string::npos, "no GROUP class without a codec");
        }
        ScopedProbes probes(false, true, true);
        hdxf::Drawing d = build();
        std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!bytes.empty(), d.last_error());
        hdxf::Drawing back;
        assert_true(back.load_from_memory(bytes.data(), bytes.size()), back.last_error());
        int found = 0;
        for (const auto& section : back.raw_sections()) {
            if (section.name != "CLASSES") continue;
            for (const auto& record : section.records) {
                std::vector<DxfGroup> g = record.groups();
                bool is_group = false;
                for (const auto& x : g) if (x.code == 1 && x.value == "GROUP") is_group = true;
                if (!is_group) continue;
                ++found;
                for (const auto& x : g) {
                    if (x.code == 2) assert_eq("AcDbGroup", x.value);
                    if (x.code == 3) {
                        // R13/R14: the older register's application; 2000+: ObjectDBX Classes
                        if (version == DxfVersion::R13 || version == DxfVersion::R14) assert_eq("\"AutoCAD\"", x.value);
                        else assert_eq("ObjectDBX Classes", x.value);
                    }
                    if (x.code == 90 && version == DxfVersion::R13) {
                        assert_eq("32768", std::string(x.value.begin() + static_cast<long>(x.value.find_first_not_of(' ')), x.value.end()));
                    }
                    if (x.code == 91) assert_eq("1", std::string(x.value.begin() + static_cast<long>(x.value.find_first_not_of(' ')), x.value.end()));
                }
            }
        }
        assert_eq(1, found, std::string("GROUP is in CLASSES once, ") + hdxf_test::version_label(version));
    }
}

// ---------------------------------------------------------------------------
// Relation descriptors in a whole drawing (B19, B104): a type with an untyped record and descriptors
// ---------------------------------------------------------------------------

TEST(type_codecs_relation_descriptors_tell_pointers_from_keys_ids_and_text_in_a_whole_drawing) {
    struct Cell { DxfVersion version; DxfFormat format; };
    const Cell cells[] = {{DxfVersion::R2000, DxfFormat::Ascii}, {DxfVersion::R2000, DxfFormat::Binary},
                          {DxfVersion::R2007, DxfFormat::Ascii}, {DxfVersion::R2018, DxfFormat::Binary}};
    for (const Cell& cell : cells) {
        const std::string where = std::string(hdxf_test::version_label(cell.version)) + " " + hdxf_test::format_label(cell.format);
        // a drawing from a file: three lines
        hdxf::Drawing base(cell.version);
        base.add_line({0, 0, 0}, {1, 0, 0});
        base.add_line({0, 1, 0}, {1, 1, 0});
        base.add_line({0, 2, 0}, {1, 2, 0});
        std::vector<uint8_t> first = base.save_to_memory(cell.format);
        assert_true(!first.empty(), where + ": " + base.last_error());
        hdxf::Drawing file;
        assert_true(file.load_from_memory(first.data(), first.size()), file.last_error());
        assert_eq(3, static_cast<int>(file.entities().size()));
        const std::string h1 = file.entities()[0].props.handle;
        const std::string h2 = file.entities()[1].props.handle;
        const std::string h3 = file.entities()[2].props.handle;

        // an untyped object with a counted list of pairs, a scalar pointer, a sort key, a handle held as text,
        // a pointer in a code outside the pointer ranges, an internal id, and a second occurrence of the subclass
        hdxf::Object o;
        o.type = hdxf::ObjectType::Unknown;
        o.handle = "FFFFF0";
        o.owner = "0";
        hdxf::UnknownObjectData u;
        u.object_type = "HDXF_PROBE";
        u.group_codes = {{100, "HdxfProbe"}, {90, "4"},
                         {340, h1}, {70, "1"}, {340, h2}, {70, "2"}, {340, h3}, {70, "3"}, {340, "FFFF"}, {70, "4"},
                         {331, h2}, {5, h2}, {350, h2}, {411, h2}, {330, h2},
                         {100, "HdxfProbe"}, {331, h2}, {1, "end"}};
        o.data = u;
        file.objects().push_back(o);
        std::vector<uint8_t> second = file.save_to_memory(cell.format);
        assert_true(!second.empty(), where + ": " + file.last_error());

        auto run = [&](bool with_descriptors) {
            TypedRegistry& registry = TypedRegistry::instance();
            if (with_descriptors) {
                TypeRelations rel;
                rel.dxf_name = "HDXF_PROBE";
                auto add = [&](int occurrence, int code, RefKind kind, RefRemoval removal) {
                    RefDescriptor d;
                    d.subclass = "HdxfProbe";
                    d.occurrence = occurrence;
                    d.code = code;
                    d.kind = kind;
                    d.removal = removal;
                    rel.refs.push_back(d);
                    return &rel.refs.back();
                };
                RefDescriptor* member = add(0, 340, RefKind::HardPointer, RefRemoval::DropPair);
                member->first_offset = 0;
                member->last_offset = 1;
                member->partner_code = 70;
                member->counter_code = 90;
                add(0, 331, RefKind::SoftPointer, RefRemoval::ZeroInPlace);
                add(0, 5, RefKind::SortKey, RefRemoval::Keep);
                add(0, 350, RefKind::ExternalText, RefRemoval::Keep);
                add(0, 411, RefKind::SoftPointer, RefRemoval::ZeroInPlace);
                add(0, 330, RefKind::InternalId, RefRemoval::Keep);
                add(1, 331, RefKind::InternalId, RefRemoval::Keep);
                registry.add_relations(rel);
            }
            hdxf::Drawing d;
            assert_true(d.load_from_memory(second.data(), second.size()), d.last_error());
            // unchanged first: the same bytes
            assert_true(d.save_to_memory(cell.format) == second, where + ": unchanged");
            // the second line is erased from the drawing
            d.entities().erase(d.entities().begin() + 1);
            std::vector<uint8_t> out = d.save_to_memory(cell.format);
            if (with_descriptors) registry.remove_relations("HDXF_PROBE");
            assert_true(!out.empty(), where + ": " + d.last_error());
            hdxf::Drawing back;
            assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
            const hdxf::Object* probe = back.find_object("FFFFF0");
            assert_true(probe != nullptr, where + ": the object is still there");
            std::vector<std::pair<int, std::string>> groups;
            for (const auto& g : probe->as_unknown().group_codes) {
                std::string v = g.second;
                while (!v.empty() && v.front() == ' ') v.erase(v.begin());
                groups.push_back({g.first, v});
            }
            return std::make_pair(groups, std::make_pair(h2, h3));
        };

        auto with = run(true);
        const std::string& dead = with.second.first;
        const std::string& kept = with.second.second;
        const std::vector<std::pair<int, std::string>> expected_with = {
            {100, "HdxfProbe"}, {90, "3"},
            {340, h1}, {70, "1"}, {340, kept}, {70, "3"}, {340, "FFFF"}, {70, "4"},     // the pair of the erased line went, the count follows
            {331, "0"},                  // a soft pointer: 0 in its place
            {5, dead},                   // a sort key stays
            {350, dead},                 // a handle held as text stays, in the range of a pointer
            {411, "0"},                  // a pointer outside the pointer ranges is cleared too
            {330, dead},                 // an internal id stays, in the range of a pointer
            {100, "HdxfProbe"}, {331, dead}, {1, "end"}};   // the second occurrence of the subclass has its own rule
        assert_true(with.first == expected_with, where + ": with descriptors the groups are not as expected");

        // without descriptors only the code ranges decide: 331, 350 and 330 become 0, 340 is 0 but its pair stays,
        // 5 and 411 are not pointers
        auto without = run(false);
        const std::vector<std::pair<int, std::string>> expected_without = {
            {100, "HdxfProbe"}, {90, "4"},
            {340, h1}, {70, "1"}, {340, "0"}, {70, "2"}, {340, kept}, {70, "3"}, {340, "FFFF"}, {70, "4"},
            {331, "0"}, {5, dead}, {350, "0"}, {411, dead}, {330, "0"},
            {100, "HdxfProbe"}, {331, "0"}, {1, "end"}};
        assert_true(without.first == expected_without, where + ": without descriptors the code ranges decide");
    }
}

// ---------------------------------------------------------------------------
// Several lists in one subclass, and the groups of a subclass that appears twice (B12)
// ---------------------------------------------------------------------------

TEST(type_codecs_two_lists_in_one_subclass_are_written_apart_when_a_record_is_written_back) {
    for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
        hdxf::Drawing base(DxfVersion::R2000);
        hdxf::Object o;
        o.type = hdxf::ObjectType::Unknown;
        o.handle = "FFFFF1";
        o.owner = "0";
        hdxf::UnknownObjectData u;
        u.object_type = "HDXF_LISTS";
        u.group_codes = {{100, "HdxfLists"}, {1, "scalar"}, {91, "2"}, {301, "a"}, {302, "b"}, {92, "2"}, {303, "x"}, {304, "y"}, {70, "5"},
                         {100, "HdxfLists"}, {1, "second occurrence"}, {91, "1"}, {301, "z"}};
        o.data = u;
        base.objects().push_back(o);
        std::vector<uint8_t> bytes = base.save_to_memory(format);
        assert_true(!bytes.empty(), base.last_error());

        // two lists in the subclass, a third in the second occurrence only
        ListSpec first;
        first.subclass = "HdxfLists";
        first.id = 1;
        first.codes = {91, 301, 302};
        ListSpec second;
        second.subclass = "HdxfLists";
        second.occurrence = 0;
        second.id = 2;
        second.codes = {92, 303, 304};
        TypedRegistry::instance().add_lists("HDXF_LISTS", {first, second});

        // the lists as groups of the record, read back
        auto lists_after = [&](const std::function<void(std::vector<std::pair<int, std::string>>&)>& edit) {
            hdxf::Drawing d;
            assert_true(d.load_from_memory(bytes.data(), bytes.size()), d.last_error());
            hdxf::Object* object = d.find_object("FFFFF1");
            assert_true(object != nullptr, "the object");
            edit(object->as_unknown().group_codes);
            std::vector<uint8_t> out = d.save_to_memory(format);
            assert_true(!out.empty(), d.last_error());
            hdxf::Drawing back;
            assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
            std::vector<std::pair<int, std::string>> g;
            for (const auto& x : back.find_object("FFFFF1")->as_unknown().group_codes) {
                std::string v = x.second;
                while (!v.empty() && v.front() == ' ') v.erase(v.begin());
                g.push_back({x.first, v});
            }
            return g;
        };

        // only the second list grows: the first and the scalars stay as they were, the second is written whole
        auto grown = lists_after([](std::vector<std::pair<int, std::string>>& g) {
            for (auto& x : g) if (x.first == 92) x.second = "3";
            for (size_t i = 0; i < g.size(); ++i) {
                if (g[i].first == 304) { g.insert(g.begin() + static_cast<long>(i) + 1, {{303, "x2"}, {304, "y2"}}); break; }
            }
        });
        const std::vector<std::pair<int, std::string>> expected = {
            {100, "HdxfLists"}, {1, "scalar"}, {91, "2"}, {301, "a"}, {302, "b"}, {92, "3"}, {303, "x"}, {304, "y"}, {303, "x2"}, {304, "y2"}, {70, "5"},
            {100, "HdxfLists"}, {1, "second occurrence"}, {91, "1"}, {301, "z"}};
        assert_true(grown == expected, "the second list written whole, the rest as it was");

        // the second occurrence (no list of its own in the specs: its 91 and 301 are a list of the same id 1 only
        // in occurrence 0's key space, since the spec says every occurrence) changes alone
        auto second_changed = lists_after([](std::vector<std::pair<int, std::string>>& g) {
            for (size_t i = 0; i < g.size(); ++i) {
                if (g[i].first == 1 && g[i].second == "second occurrence") g[i].second = "changed";
            }
        });
        assert_eq("changed", second_changed[10].second);
        assert_eq("5", second_changed[8].second);
        TypedRegistry::instance().remove_lists("HDXF_LISTS");
    }
}

TEST(type_codecs_a_run_from_a_code_and_a_whole_subclass_are_written_as_one_unit) {
    for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
        hdxf::Drawing base(DxfVersion::R2000);
        hdxf::Object o;
        o.type = hdxf::ObjectType::Unknown;
        o.handle = "FFFFF2";
        o.owner = "0";
        hdxf::UnknownObjectData u;
        u.object_type = "HDXF_RUNS";
        // a run from its first 91 to the end of the subclass (the codes inside mean different things where they
        // stand: 92 is a count the first time and a flag the second), then a subclass that is one list as a whole
        u.group_codes = {{100, "HdxfRun"}, {1, "scalar"}, {91, "2"}, {92, "5"}, {303, "x"}, {92, "6"}, {303, "y"}, {70, "9"},
                         {100, "HdxfWhole"}, {1, "w"}, {90, "1"}, {1, "v"}};
        o.data = u;
        base.objects().push_back(o);
        std::vector<uint8_t> bytes = base.save_to_memory(format);
        assert_true(!bytes.empty(), base.last_error());

        ListSpec run;
        run.subclass = "HdxfRun";
        run.shape = ListShape::FromCode;
        run.codes = {91};
        ListSpec whole;
        whole.subclass = "HdxfWhole";
        whole.shape = ListShape::WholeSubclass;
        TypedRegistry::instance().add_lists("HDXF_RUNS", {run, whole});

        auto written = [&](const std::function<void(std::vector<std::pair<int, std::string>>&)>& edit) {
            hdxf::Drawing d;
            assert_true(d.load_from_memory(bytes.data(), bytes.size()), d.last_error());
            edit(d.find_object("FFFFF2")->as_unknown().group_codes);
            std::vector<uint8_t> out = d.save_to_memory(format);
            assert_true(!out.empty(), d.last_error());
            hdxf::Drawing back;
            assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
            std::vector<std::pair<int, std::string>> g;
            for (const auto& x : back.find_object("FFFFF2")->as_unknown().group_codes) {
                std::string v = x.second;
                while (!v.empty() && v.front() == ' ') v.erase(v.begin());
                g.push_back({x.first, v});
            }
            return g;
        };
        // a group inserted inside the run stays where it was put (by key it would be appended after the 70)
        auto inserted = written([](std::vector<std::pair<int, std::string>>& g) {
            for (size_t i = 0; i < g.size(); ++i) {
                if (g[i].first == 303 && g[i].second == "x") { g.insert(g.begin() + static_cast<long>(i) + 1, {{92, "7"}, {303, "z"}}); break; }
            }
        });
        const std::vector<std::pair<int, std::string>> expected_run = {
            {100, "HdxfRun"}, {1, "scalar"}, {91, "2"}, {92, "5"}, {303, "x"}, {92, "7"}, {303, "z"}, {92, "6"}, {303, "y"}, {70, "9"},
            {100, "HdxfWhole"}, {1, "w"}, {90, "1"}, {1, "v"}};
        std::string actual_text;
        for (const auto& x : inserted) actual_text += " " + std::to_string(x.first) + "=" + x.second;
        assert_true(inserted == expected_run, "the run is written whole, in the order it was given:" + actual_text);
        // the whole subclass: a change in it is written from the drawing, the run before it as it was
        auto whole_changed = written([](std::vector<std::pair<int, std::string>>& g) {
            for (auto& x : g) if (x.first == 1 && x.second == "v") x.second = "changed";
        });
        assert_eq("changed", whole_changed[11].second);
        assert_eq("9", whole_changed[7].second);
        assert_eq("x", whole_changed[4].second);
        TypedRegistry::instance().remove_lists("HDXF_RUNS");
    }
}

// ---------------------------------------------------------------------------
// The memory budget and value semantics of the typed records
// ---------------------------------------------------------------------------

TEST(type_codecs_memory_budget_and_copying_of_typed_values) {
#if defined(__GNUC__) && !defined(__clang__)
    if (sizeof(void*) == 8) {
        // The sizes of v1.8.1: the families add their types as Boxed<T> alternatives (one pointer each) and
        // none of these grows; a family that makes them grow breaks its own build
        assert_true(sizeof(hdxf::Entity) <= 736, "sizeof(Entity) = " + std::to_string(sizeof(hdxf::Entity)));
        assert_true(sizeof(hdxf::EntityData) <= 336, "sizeof(EntityData) = " + std::to_string(sizeof(hdxf::EntityData)));
        assert_true(sizeof(hdxf::Object) <= 936, "sizeof(Object) = " + std::to_string(sizeof(hdxf::Object)));
        assert_true(sizeof(hdxf::ObjectData) <= 752, "sizeof(ObjectData) = " + std::to_string(sizeof(hdxf::ObjectData)));
        assert_true(sizeof(hdxf::Block) <= 656, "sizeof(Block) = " + std::to_string(sizeof(hdxf::Block)));
    }
#endif
    // a typed record's own data lives behind one pointer
    static_assert(sizeof(hdxf::Boxed<hdxf::EvalExprData>) == sizeof(void*), "a Boxed value is one pointer");
    static_assert(sizeof(hdxf::OptionalBoxed<hdxf::EvalExprData>) == sizeof(void*), "an OptionalBoxed value is one pointer");
    static_assert(std::is_nothrow_move_constructible_v<hdxf::Entity>, "a vector of entities moves its entities");
    static_assert(std::is_nothrow_move_constructible_v<hdxf::Boxed<hdxf::LightData>>, "a Boxed value moves");

    // copying is deep, moving leaves the copy intact
    hdxf::Entity a;
    a.type = hdxf::EntityType::Light;
    hdxf::LightData light;
    light.name = "A";
    a.data = hdxf::Boxed<hdxf::LightData>(light);
    hdxf::Entity b = a;
    b.as_light().name = "B";
    assert_eq("A", a.as_light().name);
    assert_eq("B", b.as_light().name);
    hdxf::Entity c = std::move(b);
    assert_eq("B", c.as_light().name);
    hdxf::EvalExprData x;
    x.expression = hdxf::EvalExprValue{"e", hdxf::EvalVariant{40, 2.0, 0}};
    hdxf::EvalExprData y = x;
    y.expression->value.real = 3.0;
    assert_eq(2.0, x.expression->value.real);
    assert_eq(3.0, y.expression->value.real);

    // a drawing with a typed record moves and copies as a value
    hdxf::Drawing d(DxfVersion::R2007);
    d.add_entity(a);
    hdxf::Drawing moved = std::move(d);
    assert_eq(1, static_cast<int>(moved.entities().size()));
    assert_eq("A", moved.entities()[0].as_light().name);
}

// ObjectData is the std::variant it derives from (Codex 9.S, 16): in place construction, the variant traits, std::get,
// std::get_if, std::holds_alternative and std::visit; the members get<T>() and get_if<T>() are the same
TEST(type_codecs_object_data_is_the_variant_it_derives_from) {
    static_assert(std::variant_size_v<hdxf::ObjectData> == std::variant_size_v<hdxf::ObjectDataVariant>);
    static_assert(std::is_same_v<std::variant_alternative_t<0, hdxf::ObjectData>, hdxf::DictionaryData>);
    hdxf::ObjectData in_place(std::in_place_type<hdxf::XRecordData>);
    assert_true(std::holds_alternative<hdxf::XRecordData>(in_place), "in_place_type");
    hdxf::ObjectData by_index(std::in_place_index<0>);
    assert_true(by_index.index() == 0, "in_place_index");
    hdxf::DictionaryData dictionary;
    dictionary.entries.push_back({"A", "1F", false});
    hdxf::ObjectData converted = dictionary;
    assert_eq("A", std::get<hdxf::DictionaryData>(converted).entries[0].name);
    assert_true(converted.get_if<hdxf::DictionaryData>() == std::get_if<hdxf::DictionaryData>(&converted), "get_if<T>() is std::get_if");
    assert_true(converted.get_if<hdxf::XRecordData>() == nullptr, "another alternative");
    const size_t index = std::visit([](const auto& v) { return sizeof(v); }, converted);
    assert_true(index == sizeof(hdxf::DictionaryData), "std::visit");
    hdxf::ObjectData copy = converted;
    copy = hdxf::XRecordData{};
    assert_true(std::holds_alternative<hdxf::DictionaryData>(converted) && std::holds_alternative<hdxf::XRecordData>(copy), "copy, assign");
}
