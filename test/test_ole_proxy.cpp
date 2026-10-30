#include "test_framework.hpp"
#include "type_version_matrix.hpp"
#include "typed_corpus.hpp"
#include "hdxf.hpp"
#include "TypedRecordTypes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Step 9.3 of phase 9 (plan 2026-10-27-fas-9-typer.md): OLEFRAME, OLE2FRAME, ACAD_PROXY_ENTITY and
// ACAD_PROXY_OBJECT.
//
// The AutoCAD evidence: doc/facit/acad_<2000 … 2018>_dxf_proxy[_bin].dxf, saved by AutoCAD 2025's core from
// LibreDWG's gh209_1 (AutoCAD 2010; three ACAD_PROXY_OBJECT of AutoCAD Map's classes) by
// doc/facit/fas9_ole_proxy.scr. There is no AutoCAD file with an OLEFRAME, an OLE2FRAME or an ACAD_PROXY_ENTITY
// (F9.1, F9.2: the core has no OLE server and no application that makes a proxy entity): their tests use
// records built here and are "ej testat" against AutoCAD.
// ---------------------------------------------------------------------------

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::Point3;
using namespace hdxf::internal;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

namespace {

std::filesystem::path facit(const std::string& name) { return hdxf_test::source_path("doc/facit/" + name); }

std::vector<uint8_t> read_bytes(const std::filesystem::path& path) { return hdxf_test::read_file_bytes(path); }

std::vector<uint8_t> hex(const std::string& text) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < text.size(); i += 2) out.push_back(static_cast<uint8_t>(std::stoi(text.substr(i, 2), nullptr, 16)));
    return out;
}

std::vector<uint8_t> pattern(size_t n, unsigned seed = 7) {
    std::vector<uint8_t> out(n);
    for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>((i * 31 + seed * 17 + (i >> 8)) & 0xFF);
    return out;
}

// A saved drawing's $VERSIONGUID is new at every save that changed something: the same GUID in both
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

std::vector<hdxf::Object*> proxy_objects(hdxf::Drawing& d) {
    std::vector<hdxf::Object*> out;
    for (auto& o : d.objects()) if (o.type == hdxf::ObjectType::ProxyObject) out.push_back(&o);
    return out;
}

std::vector<const hdxf::Object*> proxies_of(const hdxf::Drawing& d) {
    std::vector<const hdxf::Object*> out;
    for (const auto& o : d.objects()) if (o.type == hdxf::ObjectType::ProxyObject) out.push_back(&o);
    return out;
}

std::vector<hdxf::Entity*> entities_of(hdxf::Drawing& d, hdxf::EntityType type) {
    std::vector<hdxf::Entity*> out;
    for (auto& e : d.entities()) if (e.type == type) out.push_back(&e);
    for (auto& b : d.blocks()) for (auto& e : b.entities) if (e.type == type) out.push_back(&e);
    return out;
}

const hdxf::Entity* first_entity(const hdxf::Drawing& d, hdxf::EntityType type) {
    for (const auto& e : d.entities()) if (e.type == type) return &e;
    for (const auto& b : d.blocks()) for (const auto& e : b.entities) if (e.type == type) return &e;
    return nullptr;
}

// The CLASS records of a drawing read from a file: 1, 2, 3, 90 and 281, in order
struct ClassRow {
    std::string dxf_name, cpp_name, application;
    int flags = 0;
    bool entity = false;
    int count = -1;                     // 91, -1 when the record has none
};

std::vector<ClassRow> classes_of(const hdxf::Drawing& d) {
    std::vector<ClassRow> out;
    for (const auto& s : d.raw_sections()) {
        if (s.name != "CLASSES") continue;
        for (const auto& r : s.records) {
            ClassRow row;
            bool is_class = false;
            for (const auto& g : r.groups()) {
                auto trimmed = [](std::string v) {
                    while (!v.empty() && v.back() == ' ') v.pop_back();
                    size_t b = 0;
                    while (b < v.size() && v[b] == ' ') ++b;
                    return v.substr(b);
                };
                if (g.code == 0) is_class = trimmed(g.value) == "CLASS";
                if (g.code == 1) row.dxf_name = trimmed(g.value);
                if (g.code == 2) row.cpp_name = trimmed(g.value);
                if (g.code == 3) row.application = trimmed(g.value);
                if (g.code == 90) row.flags = std::atoi(g.value.c_str());
                if (g.code == 91) row.count = std::atoi(g.value.c_str());
                if (g.code == 281) row.entity = std::atoi(g.value.c_str()) != 0;
            }
            if (is_class) out.push_back(row);
        }
    }
    return out;
}

// The groups of a preserved record, values trimmed
std::vector<std::pair<int, std::string>> groups_of(const hdxf::RawRecord& raw) {
    std::vector<std::pair<int, std::string>> out;
    for (const auto& g : raw.groups()) {
        std::string v = g.value;
        while (!v.empty() && (v.back() == ' ' || v.back() == '\r')) v.pop_back();
        size_t b = 0;
        while (b < v.size() && v[b] == ' ') ++b;
        out.push_back({g.code, v.substr(b)});
    }
    return out;
}

std::string version_name(DxfVersion v) { return hdxf_test::version_label(v); }

// The AutoCAD files: version, ASCII and binary
struct FacitFile {
    DxfVersion version;
    const char* name;
    DxfFormat format;
};

const FacitFile kProxyFiles[] = {
    {DxfVersion::R2000, "acad_2000_dxf_proxy.dxf", DxfFormat::Ascii},
    {DxfVersion::R2000, "acad_2000_dxf_proxy_bin.dxf", DxfFormat::Binary},
    {DxfVersion::R2004, "acad_2004_dxf_proxy.dxf", DxfFormat::Ascii},
    {DxfVersion::R2004, "acad_2004_dxf_proxy_bin.dxf", DxfFormat::Binary},
    {DxfVersion::R2007, "acad_2007_dxf_proxy.dxf", DxfFormat::Ascii},
    {DxfVersion::R2007, "acad_2007_dxf_proxy_bin.dxf", DxfFormat::Binary},
    {DxfVersion::R2010, "acad_2010_dxf_proxy.dxf", DxfFormat::Ascii},
    {DxfVersion::R2010, "acad_2010_dxf_proxy_bin.dxf", DxfFormat::Binary},
    {DxfVersion::R2013, "acad_2013_dxf_proxy.dxf", DxfFormat::Ascii},
    {DxfVersion::R2013, "acad_2013_dxf_proxy_bin.dxf", DxfFormat::Binary},
    {DxfVersion::R2018, "acad_2018_dxf_proxy.dxf", DxfFormat::Ascii},
    {DxfVersion::R2018, "acad_2018_dxf_proxy_bin.dxf", DxfFormat::Binary},
};

// What AutoCAD wrote for the three proxy objects (handles 8E, 8F, 90): the same in every version except the
// stream: 2000 and 2004 have the graphics inside the data (their lengths in bits differ by the stream's end), 2007
// and later keep them apart
struct ExpectedProxy {
    const char* handle;
    const char* dxf_name;
    const char* cpp_name;
    int class_id;
    std::vector<hdxf::ProxyPointerKind> kinds;
    std::vector<std::string> targets;
};

const ExpectedProxy kExpected[] = {
    {"8E", "DMMAPMANAGER", "AcMapDMMapManager", 517, {hdxf::ProxyPointerKind::HardPointer, hdxf::ProxyPointerKind::HardPointer}, {"0", "0"}},
    {"8F", "DMSTYLELIBRARY", "AcMapDMStyleLibrary", 518, {hdxf::ProxyPointerKind::HardOwner}, {"90"}},
    {"90", "DMSTYLECATEGORY", "AcMapDMStyleCategory", 519, {}, {}},
};

bool apart(DxfVersion v) { return v >= DxfVersion::R2007; }

// The data and graphics AutoCAD wrote, by version (read from its files)
struct Stream {
    std::string graphics_hex;
    uint32_t graphics_bits;
    std::string data_hex;
    uint32_t data_bits;
};

Stream expected_stream(int index, DxfVersion v) {
    if (apart(v)) {
        static const Stream apart_streams[3] = {
            {"", 0, "90100404", 30},
            {"", 0, "40501C", 20},
            {"4391001A401CC01C001B0018401E40080014C01D001E401B0019401CC00C", 234, "406C", 12},
        };
        return apart_streams[index];
    }
    static const Stream joined[3] = {
        {"", 0, "90100404", 31},
        {"", 0, "405014", 21},
        {"", 0, "4064391001A401CC01C001B0018401E40080014C01D001E401B0019401CC03A802", 263},
    };
    return joined[index];
}

const char* kApplication = "AcMapDisplayManagement";

// ---------------------------------------------------------------------------
// Building records
// ---------------------------------------------------------------------------

hdxf::Object make_proxy_object(const std::string& handle, const std::string& class_name, int data_bits_value,
                               const std::vector<uint8_t>& data, DxfVersion version) {
    hdxf::Object o;
    o.type = hdxf::ObjectType::ProxyObject;
    o.handle = handle;
    o.owner = "0";
    hdxf::ProxyObjectData d;
    d.proxy.application_class.dxf_name = class_name;
    d.proxy.application_class.cpp_name = "AcTest" + class_name;
    d.proxy.application_class.application = "TestApplication";
    d.proxy.application_class.proxy_flags = 1025;
    d.proxy.drawing_format = hdxf::ProxyDrawingFormat{29, 226};
    d.proxy.original_format = 0;
    d.proxy.data = data;
    d.proxy.data_bits = static_cast<uint32_t>(data_bits_value);
    (void)version;
    o.data = d;
    return o;
}

hdxf::Entity make_ole2(const std::vector<uint8_t>& data, hdxf::OleObjectType type = hdxf::OleObjectType::Embedded,
                       int paper = 0) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Ole2Frame;
    e.props.layer = "0";
    e.props.paper_space = paper;
    hdxf::Ole2FrameData d;
    d.version = 2;
    d.application_name = "Paintbrush Picture";
    d.upper_left = {4.43116, 5.665992, 0.0};
    d.lower_right = {6.4188, 4.244939, 0.0};
    d.object_type = type;
    d.tile_mode = paper ? 1 : 0;
    d.data = data;
    e.data = d;
    return e;
}

hdxf::Entity make_oleframe(const std::vector<uint8_t>& data) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::OleFrame;
    e.props.layer = "0";
    hdxf::OleFrameData d;
    d.version = 2;
    d.data = data;
    e.data = d;
    return e;
}

} // namespace

// ---------------------------------------------------------------------------
// ACAD_PROXY_OBJECT: AutoCAD's files
// ---------------------------------------------------------------------------

TEST(ole_proxy_proxy_objects_of_autocads_files_are_read_with_every_field) {
    int files = 0;
    for (const FacitFile& f : kProxyFiles) {
        std::vector<uint8_t> in = read_bytes(facit(f.name));
        if (in.empty()) continue;
        ++files;
        hdxf::Drawing d;
        assert_true(d.load_from_memory(in.data(), in.size()), std::string(f.name) + ": " + d.last_error());
        std::vector<const hdxf::Object*> proxies = proxies_of(d);
        assert_eq(3, static_cast<int>(proxies.size()), std::string(f.name) + ": proxy objects");
        // no unknown record of the name is left
        for (const auto& o : d.objects()) {
            assert_false(o.type == hdxf::ObjectType::Unknown && o.as_unknown().object_type == "ACAD_PROXY_OBJECT",
                         std::string(f.name) + ": an ACAD_PROXY_OBJECT stayed untyped");
        }
        for (int i = 0; i < 3; ++i) {
            const hdxf::Object& o = *proxies[static_cast<size_t>(i)];
            const ExpectedProxy& x = kExpected[i];
            const hdxf::ProxyClassData& p = o.as_proxy_object().proxy;
            const std::string where = std::string(f.name) + " " + x.handle;
            assert_eq(std::string(x.handle), o.handle, where + ": handle");
            assert_eq(499, p.class_id, where + ": 90");
            assert_eq(x.class_id, p.application_class_id, where + ": 91");
            assert_eq(std::string(x.dxf_name), p.application_class.dxf_name, where + ": the class's DXF name");
            assert_eq(std::string(x.cpp_name), p.application_class.cpp_name, where + ": the class's C++ name");
            assert_eq(std::string(kApplication), p.application_class.application, where + ": the application");
            assert_eq(1024, p.application_class.proxy_flags, where + ": the class's flags" +
                                                                 (f.version == DxfVersion::R2000 || f.version == DxfVersion::R2004 ? " (33792 in 2000)" : ""));
            assert_true(p.drawing_format.has_value(), where + ": the drawing format");
            assert_eq(29, p.drawing_format->dwg_version, where + ": the drawing's version");
            assert_eq(226, p.drawing_format->maintenance_version, where + ": the maintenance release");
            assert_eq(0, p.original_format, where + ": 70");
            Stream s = expected_stream(i, f.version);
            assert_true(p.data == hex(s.data_hex), where + ": the data bytes");
            assert_eq(static_cast<int>(s.data_bits), static_cast<int>(p.data_bits), where + ": the data's length in bits");
            assert_true(p.graphics == hex(s.graphics_hex), where + ": the graphics bytes");
            assert_eq(static_cast<int>(s.graphics_bits), static_cast<int>(p.graphics_bits), where + ": the graphics' length in bits");
            assert_eq(static_cast<int>(x.kinds.size()), static_cast<int>(p.references.size()), where + ": the object ids");
            for (size_t k = 0; k < x.kinds.size() && k < p.references.size(); ++k) {
                assert_true(p.references[k].kind == x.kinds[k], where + ": the pointer kind of id " + std::to_string(k));
                assert_eq(x.targets[k], p.references[k].handle, where + ": id " + std::to_string(k));
            }
            assert_false(o.raw.empty(), where + ": the preserved record");
        }
    }
    if (files == 0) hdxf_test::skip("doc/facit/acad_*_dxf_proxy*.dxf is not here (gitignored: the main copy has it)");
}

TEST(ole_proxy_proxy_objects_are_written_back_byte_for_byte_and_from_their_typed_values) {
    int files = 0;
    for (const FacitFile& f : kProxyFiles) {
        std::vector<uint8_t> in = read_bytes(facit(f.name));
        if (in.empty()) continue;
        ++files;
        hdxf::Drawing d;
        assert_true(d.load_from_memory(in.data(), in.size()), d.last_error());
        // B12: nothing changed, the same bytes
        std::vector<uint8_t> out = d.save_to_memory(f.format);
        assert_true(out == in, std::string(f.name) + ": unchanged, " + first_difference(in, out));

        // With the preserved records emptied the codec writes them anew: AutoCAD's own groups, in its order
        std::vector<std::vector<std::pair<int, std::string>>> original;
        for (hdxf::Object* o : proxy_objects(d)) {
            original.push_back(groups_of(o->raw));
            o->raw = hdxf::RawRecord();
        }
        out = d.save_to_memory(f.format);
        assert_true(!out.empty(), std::string(f.name) + ": " + d.last_error());
        hdxf::Drawing again;
        assert_true(again.load_from_memory(out.data(), out.size()), std::string(f.name) + ": " + again.last_error());
        std::vector<const hdxf::Object*> anew = proxies_of(again);
        assert_eq(3, static_cast<int>(anew.size()), std::string(f.name) + ": proxy objects written anew");
        for (size_t i = 0; i < anew.size(); ++i) {
            assert_true(groups_of(anew[i]->raw) == original[i],
                        std::string(f.name) + " " + anew[i]->handle + ": written anew as AutoCAD writes it");
        }
        // and the other format holds the same model
        DxfFormat other = f.format == DxfFormat::Ascii ? DxfFormat::Binary : DxfFormat::Ascii;
        std::vector<uint8_t> converted = again.save_to_memory(other);
        assert_true(!converted.empty(), again.last_error());
        hdxf::Drawing third;
        assert_true(third.load_from_memory(converted.data(), converted.size()), third.last_error());
        std::vector<const hdxf::Object*> same = proxies_of(third);
        assert_eq(3, static_cast<int>(same.size()), "the other format");
        for (size_t i = 0; i < same.size(); ++i) {
            const auto& a = anew[i]->as_proxy_object().proxy;
            const auto& b = same[i]->as_proxy_object().proxy;
            assert_true(a.data == b.data && a.graphics == b.graphics && a.data_bits == b.data_bits &&
                            a.graphics_bits == b.graphics_bits && a.references.size() == b.references.size() &&
                            a.application_class_id == b.application_class_id,
                        std::string(f.name) + ": the same proxy in the other format");
        }
    }
    if (files == 0) hdxf_test::skip("doc/facit/acad_*_dxf_proxy*.dxf is not here");
}

TEST(ole_proxy_a_changed_proxy_is_rewritten_as_a_unit_and_the_others_stay) {
    for (const FacitFile& f : kProxyFiles) {
        if (f.version != DxfVersion::R2010 && f.version != DxfVersion::R2018 && f.version != DxfVersion::R2000) continue;
        std::vector<uint8_t> in = read_bytes(facit(f.name));
        if (in.empty()) continue;
        hdxf::Drawing d;
        assert_true(d.load_from_memory(in.data(), in.size()), d.last_error());
        std::vector<hdxf::Object*> proxies = proxy_objects(d);
        auto before = [&](size_t i) { return groups_of(proxies[i]->raw); };
        std::vector<std::vector<std::pair<int, std::string>>> raws = {before(0), before(1), before(2)};

        // a byte of the data changed (its length stays), a pointer added to the third, the second's removed
        proxies[0]->as_proxy_object().proxy.data[0] ^= 0xFF;
        proxies[2]->as_proxy_object().proxy.references.push_back({hdxf::ProxyPointerKind::SoftPointer, "8D"});
        proxies[1]->as_proxy_object().proxy.references.clear();
        std::vector<uint8_t> out = d.save_to_memory(f.format);
        assert_true(!out.empty(), d.last_error());
        hdxf::Drawing r;
        assert_true(r.load_from_memory(out.data(), out.size()), r.last_error());
        std::vector<const hdxf::Object*> back = proxies_of(r);
        assert_eq(3, static_cast<int>(back.size()), "three proxies");
        const auto& p0 = back[0]->as_proxy_object().proxy;
        const auto& p1 = back[1]->as_proxy_object().proxy;
        const auto& p2 = back[2]->as_proxy_object().proxy;
        assert_eq(0x90 ^ 0xFF, static_cast<int>(p0.data[0]), std::string(f.name) + ": the changed byte");
        assert_true(p1.references.empty(), std::string(f.name) + ": the pointer removed");
        assert_eq(1, static_cast<int>(p2.references.size()), std::string(f.name) + ": the pointer added");
        assert_true(p2.references[0].kind == hdxf::ProxyPointerKind::SoftPointer && p2.references[0].handle == "8D", "its kind and target");
        // the head and the class are as they were
        assert_eq(std::string("DMMAPMANAGER"), p0.application_class.dxf_name, "the class");
        assert_eq(517, p0.application_class_id, "91");
        assert_true(groups_of(back[0]->raw).size() == raws[0].size(), "the same groups, the same count");
        // 8E's length and everything else of its subclass is what it was: only the byte differs
        auto changed = groups_of(back[0]->raw);
        int differing = 0;
        for (size_t i = 0; i < changed.size() && i < raws[0].size(); ++i) differing += changed[i] != raws[0][i];
        assert_eq(1, differing, std::string(f.name) + ": exactly one group of the first record changed");
    }
}

TEST(ole_proxy_a_removed_target_becomes_a_null_id_in_place_with_a_warning) {
    for (const FacitFile& f : kProxyFiles) {
        if (f.version != DxfVersion::R2010 && f.version != DxfVersion::R2018) continue;
        std::vector<uint8_t> in = read_bytes(facit(f.name));
        if (in.empty()) continue;
        hdxf::Drawing d;
        assert_true(d.load_from_memory(in.data(), in.size()), d.last_error());
        // 8F owns 90 (360 90): remove 90, the owned object
        auto& objects = d.objects();
        objects.erase(std::remove_if(objects.begin(), objects.end(), [](const hdxf::Object& o) { return o.handle == "90"; }),
                      objects.end());
        std::vector<uint8_t> out = d.save_to_memory(f.format);
        assert_true(!out.empty(), d.last_error());
        bool warned = false;
        for (const auto& w : d.warnings()) warned = warned || (w.find("ACAD_PROXY_OBJECT 8F") != std::string::npos && w.find("360 90") != std::string::npos);
        assert_true(warned, std::string(f.name) + ": a warning names the pointer that was set to 0");
        hdxf::Drawing r;
        assert_true(r.load_from_memory(out.data(), out.size()), r.last_error());
        std::vector<const hdxf::Object*> back = proxies_of(r);
        assert_eq(2, static_cast<int>(back.size()), "two proxies are left");
        const auto& p = back[1]->as_proxy_object().proxy;
        assert_eq(1, static_cast<int>(p.references.size()), "the id is still there, by place");
        assert_true(p.references[0].kind == hdxf::ProxyPointerKind::HardOwner, "its kind is kept");
        assert_eq(std::string("0"), p.references[0].handle, "its target is the null id");
        // the null ids the file had are untouched, and the first record is as it was
        assert_eq(2, static_cast<int>(back[0]->as_proxy_object().proxy.references.size()), "340 0, 340 0");
    }
}

namespace {

// An ASCII file as lines (any line ending) and the ending itself
struct Lines {
    std::vector<std::string> lines;
    std::string eol = "\r\n";

    explicit Lines(const std::vector<uint8_t>& bytes) {
        std::string text(bytes.begin(), bytes.end());
        if (text.find("\r\n") == std::string::npos) eol = "\n";
        size_t at = 0;
        while (at < text.size()) {
            size_t end = text.find(eol, at);
            if (end == std::string::npos) end = text.size();
            lines.push_back(text.substr(at, end - at));
            at = end + eol.size();
        }
    }

    static std::string trim(const std::string& s) {
        size_t b = 0, e = s.size();
        while (b < e && s[b] == ' ') ++b;
        while (e > b && s[e - 1] == ' ') --e;
        return s.substr(b, e - b);
    }

    // The line index of group `code` (the `ordinal`th one) of the record with `handle` (its 5 group), or npos; the
    // line is the code's, the value the next
    size_t find(const std::string& type, const std::string& handle, const std::string& code, size_t ordinal = 0) const {
        for (size_t i = 0; i + 3 < lines.size(); i += 2) {
            if (trim(lines[i]) != "0" || trim(lines[i + 1]) != type) continue;
            if (trim(lines[i + 2]) != "5" || trim(lines[i + 3]) != handle) continue;
            size_t seen = 0;
            for (size_t k = i + 2; k + 1 < lines.size() && trim(lines[k]) != "0"; k += 2) {
                if (trim(lines[k]) == code && seen++ == ordinal) return k;
            }
        }
        return std::string::npos;
    }

    std::vector<uint8_t> bytes() const {
        std::string out;
        for (const auto& l : lines) out += l + eol;
        return std::vector<uint8_t>(out.begin(), out.end());
    }
};

} // namespace

TEST(ole_proxy_proxy_objects_with_a_malformed_class_stay_as_read_with_a_warning) {
    std::vector<uint8_t> in = read_bytes(facit("acad_2010_dxf_proxy.dxf"));
    if (in.empty()) hdxf_test::skip("doc/facit/acad_2010_dxf_proxy.dxf is not here");
    struct Mutation {
        const char* what;
        const char* handle;
        const char* code;
        size_t ordinal;
        const char* value;      // the new value; null: the group is taken away
        bool value_line_only;   // false: the whole group (code and value) goes
    };
    const Mutation mutations[] = {
        {"a data length (161) that does not fit its bytes", "8E", "161", 0, "                99", true},
        {"a data length that is no number", "8E", "161", 0, "thirty", true},
        {"a negative graphics length", "8E", "162", 0, "                -4", true},
        {"an id list that does not end in 0", "8E", "94", 0, "        7", true},
        {"a missing class id", "8E", "91", 0, nullptr, false},
        {"a graphics length that does not fit its bytes", "90", "162", 0, "                 9", true},
    };
    for (const Mutation& m : mutations) {
        Lines file(in);
        size_t at = file.find("ACAD_PROXY_OBJECT", m.handle, m.code, m.ordinal);
        assert_true(at != std::string::npos, std::string("the group of ") + m.what + " is in the AutoCAD file");
        if (m.value) file.lines[at + 1] = m.value;
        else file.lines.erase(file.lines.begin() + static_cast<long>(at), file.lines.begin() + static_cast<long>(at) + 2);
        std::vector<uint8_t> bytes = file.bytes();
        hdxf::Drawing d;
        assert_true(d.load_from_memory(bytes.data(), bytes.size()), std::string(m.what) + ": " + d.last_error());
        int untyped = 0, typed = 0;
        for (const auto& o : d.objects()) {
            if (o.type == hdxf::ObjectType::Unknown && o.as_unknown().object_type == "ACAD_PROXY_OBJECT") ++untyped;
            if (o.type == hdxf::ObjectType::ProxyObject) ++typed;
        }
        assert_eq(1, untyped, std::string(m.what) + ": the record is kept untyped");
        assert_eq(2, typed, std::string(m.what) + ": the others are typed");
        bool warned = false;
        for (const auto& w : d.warnings()) warned = warned || (w.find("ACAD_PROXY_OBJECT") != std::string::npos && w.find("kept as it was read") != std::string::npos);
        assert_true(warned, std::string(m.what) + ": a warning says so");
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(out == bytes, std::string(m.what) + ": written back as it was read, " + first_difference(bytes, out));
    }
    // a group the grammar has no place for (a later subversion?), after the id list or inside it: the typed values could not
    // hold it, so the record stays untyped with a warning and is written back as it was (9.S, Codex 5: a proxy that looks typed
    // but needs its preserved record must not exist)
    for (const char* after : {"94", "70"}) {
        Lines file(in);
        size_t at = file.find("ACAD_PROXY_OBJECT", "8E", after, 0);
        assert_true(at != std::string::npos, std::string(after) + " of 8E");
        file.lines.insert(file.lines.begin() + static_cast<long>(at) + 2, {"  1", "later"});
        std::vector<uint8_t> bytes = file.bytes();
        hdxf::Drawing d;
        assert_true(d.load_from_memory(bytes.data(), bytes.size()), d.last_error());
        assert_eq(2, static_cast<int>(proxy_objects(d).size()), std::string("a group 1 after ") + after + ": the record is untyped");
        bool warned = false;
        for (const auto& w : d.warnings()) warned = warned || (w.find("8E") != std::string::npos && w.find("kept as it was read") != std::string::npos);
        assert_true(warned, "a warning says so");
        assert_true(d.save_to_memory(DxfFormat::Ascii) == bytes, "and it is written back as it was");
    }
    // 91 below 500 names no class (Codex 15): the lowest int32 is untyped with a warning, nothing computed from it
    for (const char* value : {"-2147483648", "499"}) {
        Lines file(in);
        size_t at = file.find("ACAD_PROXY_OBJECT", "8E", "91", 0);
        assert_true(at != std::string::npos, "91 of 8E");
        file.lines[at + 1] = std::string("      ") + value;
        std::vector<uint8_t> bytes = file.bytes();
        hdxf::Drawing d;
        assert_true(d.load_from_memory(bytes.data(), bytes.size()), d.last_error());
        assert_eq(2, static_cast<int>(proxy_objects(d).size()), std::string("91 = ") + value + ": the record is untyped");
        bool warned = false;
        for (const auto& w : d.warnings()) warned = warned || (w.find("class id (91)") != std::string::npos && w.find(value) != std::string::npos);
        assert_true(warned, std::string("91 = ") + value + ": a warning names it");
        assert_true(d.save_to_memory(DxfFormat::Ascii) == bytes, std::string("91 = ") + value + ": written back as it was");
    }
}

TEST(ole_proxy_other_producers_proxy_objects_of_r13_and_r14_are_typed_and_kept) {
    // ezdxf's binary R13 and R14 files (the ODA form: no AutoCAD file): 49 proxy objects each in the forms
    // "90 91 93 310", with and without an id list and a second 310 stream, in AcDbZombieObject (R13) and
    // AcDbProxyObject (R14)
    struct Sample {
        const char* path;
        int count;
        bool zombie;
        DxfFormat format;
    };
    const Sample samples[] = {{"ezdxf/bin_dxf_r13.dxf", 49, true, DxfFormat::Binary}, {"ezdxf/bin_dxf_r14.dxf", 49, false, DxfFormat::Binary}};
    int checked = 0;
    for (const Sample& s : samples) {
        std::filesystem::path p = hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / s.path;
        std::vector<uint8_t> in = read_bytes(p);
        if (in.empty()) continue;
        ++checked;
        hdxf::Drawing d;
        assert_true(d.load_from_memory(in.data(), in.size()), std::string(s.path) + ": " + d.last_error());
        std::vector<const hdxf::Object*> proxies = proxies_of(d);
        assert_true(s.count == static_cast<int>(proxies.size()), std::string(s.path) + ": typed proxy objects: " +
                                                                     std::to_string(proxies.size()) + ", not " + std::to_string(s.count));
        int with_references = 0;
        for (const hdxf::Object* o : proxies) {
            const hdxf::ProxyClassData& pc = o->as_proxy_object().proxy;
            assert_eq(s.zombie ? 1 : 0, pc.zombie_marker ? 1 : 0, std::string(s.path) + ": the subclass name's form");
            assert_eq(499, pc.class_id, "90");
            assert_false(pc.drawing_format.has_value(), "no 95 in this form");
            assert_true(pc.data.size() == static_cast<size_t>((pc.data_bits + 7) / 8), "the data's length");
            if (!pc.references.empty()) ++with_references;
        }
        assert_true(with_references >= 3, std::string(s.path) + ": the records with an id list are typed");
        // the class of a proxy is the one its 91 names
        for (const hdxf::Object* o : proxies) {
            const hdxf::ProxyClassData& pc = o->as_proxy_object().proxy;
            assert_false(pc.application_class.dxf_name.empty(), std::string(s.path) + ": 91 = " + std::to_string(pc.application_class_id) + " is a class of CLASSES");
        }
        std::vector<uint8_t> out = d.save_to_memory(s.format);
        assert_true(out == in, std::string(s.path) + ": unchanged, " + first_difference(in, out));
    }
    if (checked == 0) hdxf_test::skip("the pinned test data is not downloaded");
}

// ---------------------------------------------------------------------------
// New proxy objects: every version, both formats, the classes
// ---------------------------------------------------------------------------

TEST(ole_proxy_a_new_proxy_object_is_generated_in_the_versions_the_matrix_says) {
    const ObjectCodec* codec = TypedRegistry::instance().find_object("ACAD_PROXY_OBJECT");
    assert_true(codec != nullptr, "registered");
    assert_eq(3, codec->step, "registered by 9.3");
    const std::vector<uint8_t> data = pattern(37);
    auto add = [&](hdxf::Drawing& d) {
        hdxf::Object o = make_proxy_object("FFF4", "TESTOBJ", 290, data, d.version());
        o.as_proxy_object().proxy.references.push_back({hdxf::ProxyPointerKind::SoftPointer, "0"});
        d.objects().push_back(o);
    };
    auto verify = [&](const hdxf::Drawing& d, DxfVersion, DxfFormat) -> std::string {
        const hdxf::Object* o = d.find_object("FFF4");
        if (!o || o->type != hdxf::ObjectType::ProxyObject) return "the proxy is not there as a proxy object";
        const hdxf::ProxyClassData& p = o->as_proxy_object().proxy;
        if (p.data != data || p.data_bits != 290) return "its data";
        if (p.application_class.dxf_name != "TESTOBJ") return "its class: " + p.application_class.dxf_name;
        if (p.class_id != 499) return "90";
        if (p.references.size() != 1 || p.references[0].kind != hdxf::ProxyPointerKind::SoftPointer) return "its id list";
        return {};
    };
    hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("ACAD_PROXY_OBJECT", codec->versions, add, verify);
    // R14 … 2018 in two formats are generated (14); R12 has a named error; R13's form is not established
    assert_eq(14, report.generated);
    assert_eq(2, report.named_errors);
    assert_eq(2, report.not_established);
    // AutoCAD saved the proxy objects in 2000 … 2018 (12 cells) and an R12 file without them (2)
    assert_eq(14, report.autocad_verified);
    assert_eq(4, report.autocad_not_savable);
    assert_eq(0, report.autocad_not_tested);
}

TEST(ole_proxy_a_new_proxy_object_gets_the_classes_ids_by_the_order_the_classes_are_written) {
    for (DxfVersion version : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2018,
                               DxfVersion::R14}) {
        for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const std::string where = std::string(version_name(version)) + " " + hdxf_test::format_label(format);
            auto build = [&](bool a_first) {
                hdxf::Drawing d(version);
                hdxf::Object a = make_proxy_object("FFF1", "CLASS_A", 16, {1, 2}, version);
                hdxf::Object b = make_proxy_object("FFF2", "CLASS_B", 8, {3}, version);
                hdxf::Object a2 = make_proxy_object("FFF3", "CLASS_A", 8, {4}, version);
                b.as_proxy_object().proxy.application_class.proxy_flags = 4095;
                d.objects().push_back(a_first ? a : b);
                d.objects().push_back(a_first ? b : a);
                d.objects().push_back(a2);
                return d;
            };
            for (bool a_first : {true, false}) {
                hdxf::Drawing d = build(a_first);
                std::vector<uint8_t> out = d.save_to_memory(format);
                assert_true(!out.empty(), where + ": " + d.last_error());
                hdxf::Drawing r;
                assert_true(r.load_from_memory(out.data(), out.size()), where + ": " + r.last_error());
                std::vector<ClassRow> classes = classes_of(r);
                auto position = [&](const std::string& name) {
                    for (size_t i = 0; i < classes.size(); ++i) if (classes[i].dxf_name == name) return static_cast<int>(i);
                    return -1;
                };
                int a_at = position("CLASS_A"), b_at = position("CLASS_B");
                assert_true(a_at >= 0 && b_at >= 0, where + ": both classes are in CLASSES");
                assert_true((a_first ? a_at < b_at : b_at < a_at), where + ": in order of first use");
                assert_eq(1, std::abs(a_at - b_at), where + ": next to each other");
                // 2004 and later: the number of proxies of each; the flags and the C++ name as given
                if (version >= DxfVersion::R2004) {
                    assert_eq(2, classes[static_cast<size_t>(a_at)].count, where + ": CLASS_A has two proxies");
                    assert_eq(1, classes[static_cast<size_t>(b_at)].count, where + ": CLASS_B has one");
                }
                assert_eq(std::string("AcTestCLASS_A"), classes[static_cast<size_t>(a_at)].cpp_name, where + ": the C++ name");
                assert_eq(std::string("TestApplication"), classes[static_cast<size_t>(a_at)].application, where + ": the application");
                assert_eq(version == DxfVersion::R13 ? 32768 : 1025, classes[static_cast<size_t>(a_at)].flags, where + ": the flags");
                assert_eq(4095, classes[static_cast<size_t>(b_at)].flags, where + ": CLASS_B's flags");
                assert_false(classes[static_cast<size_t>(a_at)].entity, where + ": an object class");
                // the id (91) is 500 + the position, and read back it names the class
                for (const hdxf::Object* o : proxy_objects(r)) {
                    const hdxf::ProxyClassData& pc = o->as_proxy_object().proxy;
                    const int at = pc.application_class.dxf_name == "CLASS_A" ? a_at : b_at;
                    assert_eq(500 + at, pc.application_class_id, where + ": " + o->handle + " 91");
                }
            }
        }
    }
}

TEST(ole_proxy_2000_has_the_class_name_as_xdata_and_a_stream_apart_from_the_data_only_from_2007) {
    // 2000: ACAD / AcRTDxfName / the class's name (AutoCAD 2025's 2000 file); 2004 and later have none
    for (DxfVersion version : {DxfVersion::R2000, DxfVersion::R2004}) {
        hdxf::Drawing d(version);
        d.objects().push_back(make_proxy_object("FFF1", "NAMED", 8, {9}, version));
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), d.last_error());
        hdxf::Drawing r;
        assert_true(r.load_from_memory(out.data(), out.size()), r.last_error());
        const hdxf::Object* o = proxies_of(r)[0];
        bool has = false;
        for (const auto& x : o->xdata) {
            for (size_t i = 0; i + 1 < x.items.size(); ++i) has = has || (x.app_name == "ACAD" && x.items[i].value == "AcRTDxfName" && x.items[i + 1].value == "NAMED");
        }
        assert_eq(version == DxfVersion::R2000 ? 1 : 0, has ? 1 : 0, std::string(version_name(version)) + ": the round trip name");
    }
    // graphics apart from the data are 2007's: an error, with nothing written, before 2007
    for (DxfVersion version : {DxfVersion::R2000, DxfVersion::R2004}) {
        hdxf::Drawing d(version);
        hdxf::Object o = make_proxy_object("FFF1", "NAMED", 8, {9}, version);
        o.as_proxy_object().proxy.graphics = {1, 2, 3};
        o.as_proxy_object().proxy.graphics_bits = 24;
        d.objects().push_back(o);
        assert_true(d.save_to_memory(DxfFormat::Ascii).empty(), "graphics apart from the data cannot be written as 2000 or 2004");
        assert_true(d.last_error().find("graphics apart from the object data") != std::string::npos, d.last_error());
    }
    for (DxfVersion version : {DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2018}) {
        for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
            hdxf::Drawing d(version);
            hdxf::Object o = make_proxy_object("FFF1", "NAMED", 8, {9}, version);
            o.as_proxy_object().proxy.graphics = pattern(300, 3);
            o.as_proxy_object().proxy.graphics_bits = 300 * 8 - 3;       // not a multiple of 8
            d.objects().push_back(o);
            std::vector<uint8_t> out = d.save_to_memory(format);
            assert_true(!out.empty(), d.last_error());
            hdxf::Drawing r;
            assert_true(r.load_from_memory(out.data(), out.size()), r.last_error());
            const hdxf::ProxyClassData& p = proxies_of(r)[0]->as_proxy_object().proxy;
            assert_true(p.graphics == pattern(300, 3), "the graphics are kept apart");
            assert_eq(300 * 8 - 3, static_cast<int>(p.graphics_bits), "their length in bits");
            assert_eq(8, static_cast<int>(p.data_bits), "the data's");
        }
    }
}

TEST(ole_proxy_lengths_that_do_not_fit_their_bytes_are_refused_before_a_file_is_made) {
    hdxf::Drawing d(DxfVersion::R2010);
    hdxf::Object o = make_proxy_object("FFF1", "BAD", 99, {1, 2}, DxfVersion::R2010);      // 2 bytes hold up to 16 bits
    d.objects().push_back(o);
    std::filesystem::path path = std::filesystem::temp_directory_path() / "hdxf_proxy_bad_length.dxf";
    std::filesystem::remove(path);
    assert_false(d.save_as(path), "refused");
    assert_true(d.last_error().find("do not hold 99 bits") != std::string::npos, d.last_error());
    assert_false(std::filesystem::exists(path), "no partial file");
}

// ---------------------------------------------------------------------------
// ACAD_PROXY_ENTITY
// ---------------------------------------------------------------------------

namespace {

hdxf::Entity make_proxy_entity(const std::string& class_name) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::ProxyEntity;
    e.props.layer = "0";
    hdxf::ProxyEntityData d;
    d.proxy.application_class.dxf_name = class_name;
    d.proxy.application_class.cpp_name = "AcTest" + class_name;
    d.proxy.application_class.application = "TestApplication";
    d.proxy.application_class.proxy_flags = 1025;
    d.proxy.application_class.entity = true;
    d.proxy.drawing_format = hdxf::ProxyDrawingFormat{29, 226};
    d.proxy.data = pattern(21, 5);
    d.proxy.data_bits = 165;
    d.proxy.references.push_back({hdxf::ProxyPointerKind::HardPointer, "0"});
    d.proxy.references.push_back({hdxf::ProxyPointerKind::SoftOwner, "0"});
    e.data = d;
    return e;
}

} // namespace

TEST(ole_proxy_a_new_proxy_entity_is_generated_in_the_versions_the_matrix_says) {
    const EntityCodec* codec = TypedRegistry::instance().find_entity("ACAD_PROXY_ENTITY");
    assert_true(codec != nullptr, "registered");
    auto add = [](hdxf::Drawing& d) {
        hdxf::Entity e = make_proxy_entity("TESTENT");
        e.as_proxy_entity().entity_graphics = {};
        d.add_entity(e);
    };
    auto verify = [](const hdxf::Drawing& d, DxfVersion, DxfFormat) -> std::string {
        const hdxf::Entity* e = first_entity(d, hdxf::EntityType::ProxyEntity);
        if (!e) return "the proxy entity is not there as one";
        const hdxf::ProxyClassData& p = e->as_proxy_entity().proxy;
        if (p.class_id != 498) return "90";
        if (p.data != pattern(21, 5) || p.data_bits != 165) return "its data";
        if (p.application_class.dxf_name != "TESTENT" || !p.application_class.entity) return "its class";
        if (p.references.size() != 2 || p.references[1].kind != hdxf::ProxyPointerKind::SoftOwner) return "its id list";
        return {};
    };
    hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("ACAD_PROXY_ENTITY", codec->versions, add, verify);
    // R14 … 2018 (16 cells); R12 and R13 have the named error "requires DXF R14 or later"
    assert_eq(14, report.generated);
    assert_eq(4, report.named_errors);
    assert_eq(0, report.not_established);
    // AutoCAD's files have the proxy entity of DXF data (TurboCAD's point, step 9.S) in 2000, 2004, 2010, 2013 and 2018 and none
    // in R12; 2007 is the point's own version there (its own record): ej testat
    assert_eq(12, report.autocad_verified);
    assert_eq(2, report.autocad_not_tested);
}

TEST(ole_proxy_a_proxy_entity_keeps_its_own_graphics_apart_from_the_classes_and_follows_proxygraphics) {
    for (DxfVersion version : {DxfVersion::R2000, DxfVersion::R2010, DxfVersion::R2018}) {
        for (bool enabled : {true, false}) {
            hdxf::Drawing d(version);
            d.set_header_int("$PROXYGRAPHICS", 70, enabled ? 1 : 0);
            hdxf::Entity e = make_proxy_entity("GFX");
            e.as_proxy_entity().entity_graphics = pattern(260, 9);
            d.add_entity(e);
            std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
            assert_true(!out.empty(), d.last_error());
            hdxf::Drawing r;
            assert_true(r.load_from_memory(out.data(), out.size()), r.last_error());
            const hdxf::Entity* back = first_entity(r, hdxf::EntityType::ProxyEntity);
            assert_true(back != nullptr, "read back");
            // $PROXYGRAPHICS 0: AutoCAD saves no proxy graphics; the class's own data stays
            assert_eq(enabled ? 260 : 0, static_cast<int>(back->as_proxy_entity().entity_graphics.size()),
                      std::string(version_name(version)) + (enabled ? ": graphics kept" : ": PROXYGRAPHICS 0"));
            assert_true(back->as_proxy_entity().proxy.data == pattern(21, 5), "the class data is untouched");
        }
    }
}

TEST(ole_proxy_a_proxy_entity_class_is_an_entity_class_in_classes_and_counts_its_proxies) {
    hdxf::Drawing d(DxfVersion::R2018);
    d.add_entity(make_proxy_entity("ENTCLASS"));
    d.add_entity(make_proxy_entity("ENTCLASS"));
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), d.last_error());
    hdxf::Drawing r;
    assert_true(r.load_from_memory(out.data(), out.size()), r.last_error());
    int found = 0;
    for (const ClassRow& c : classes_of(r)) {
        if (c.dxf_name != "ENTCLASS") continue;
        ++found;
        assert_true(c.entity, "281 is 1");
        assert_eq(2, c.count, "two proxies");
        assert_eq(1025, c.flags, "flags");
    }
    assert_eq(1, found, "one CLASS record, not two");
}

// ---------------------------------------------------------------------------
// OLEFRAME and OLE2FRAME
// ---------------------------------------------------------------------------

TEST(ole_proxy_ole2frame_is_generated_with_any_payload_in_every_version_it_exists_in) {
    const EntityCodec* codec = TypedRegistry::instance().find_entity("OLE2FRAME");
    assert_true(codec != nullptr, "registered");
    assert_eq(3, codec->step, "registered by 9.3");
    // the payload's size: nothing, one byte, the chunk boundaries, one more, a big one
    for (size_t size : {size_t(0), size_t(1), size_t(126), size_t(127), size_t(128), size_t(254), size_t(255), size_t(5000), size_t(100000)}) {
        const std::vector<uint8_t> data = pattern(size);
        auto add = [&](hdxf::Drawing& d) { d.add_entity(make_ole2(data)); };
        auto verify = [&](const hdxf::Drawing& d, DxfVersion, DxfFormat) -> std::string {
            const hdxf::Entity* e = first_entity(d, hdxf::EntityType::Ole2Frame);
            if (!e) return "the frame is not there as an OLE2FRAME";
            const hdxf::Ole2FrameData& f = e->as_ole2_frame();
            if (f.data != data) return "the payload (" + std::to_string(f.data.size()) + " bytes)";
            if (f.application_name != "Paintbrush Picture" || f.version != 2) return "its name and version";
            if (f.object_type != hdxf::OleObjectType::Embedded || f.tile_mode != 0) return "its type and space";
            if (f.upper_left.x != 4.43116 || f.upper_left.y != 5.665992 || f.lower_right.x != 6.4188 || f.lower_right.y != 4.244939) return "its corners";
            return {};
        };
        if (size != 0 && size != 127 && size != 100000) {
            // the whole matrix for a few sizes keeps the run short: the others in 2010 and 2013 binary and ASCII
            for (DxfVersion v : {DxfVersion::R2010}) {
                for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
                    hdxf::Drawing d(v);
                    add(d);
                    std::vector<uint8_t> out = d.save_to_memory(f);
                    assert_true(!out.empty(), d.last_error());
                    hdxf::Drawing r;
                    assert_true(r.load_from_memory(out.data(), out.size()), r.last_error());
                    assert_eq(std::string(), verify(r, v, f), "payload of " + std::to_string(size));
                }
            }
            continue;
        }
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("OLE2FRAME", codec->versions, add, verify);
        assert_eq(14, report.generated, "R14 … 2018, both formats");
        assert_eq(4, report.named_errors, "R12 and R13");
    }
}

TEST(ole_proxy_oleframe_is_generated_in_r13_and_in_2000_to_2018) {
    const EntityCodec* codec = TypedRegistry::instance().find_entity("OLEFRAME");
    assert_true(codec != nullptr, "registered");
    const std::vector<uint8_t> data = pattern(1000, 2);
    auto add = [&](hdxf::Drawing& d) { d.add_entity(make_oleframe(data)); };
    auto verify = [&](const hdxf::Drawing& d, DxfVersion, DxfFormat) -> std::string {
        const hdxf::Entity* e = first_entity(d, hdxf::EntityType::OleFrame);
        if (!e) return "the frame is not there as an OLEFRAME";
        if (e->as_ole_frame().data != data || e->as_ole_frame().version != 2) return "its payload and version";
        return {};
    };
    hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("OLEFRAME", codec->versions, add, verify);
    // R13 and 2000 … 2018 (14 cells); R12 has the named error; R14 has its OLE2FRAME and claims nothing
    assert_eq(14, report.generated);
    assert_eq(2, report.named_errors);
    assert_eq(2, report.not_established);
    assert_eq(0, report.autocad_verified);
}

TEST(ole_proxy_ole2frame_in_paper_space_link_and_static_kinds_and_future_values) {
    for (hdxf::OleObjectType kind : {hdxf::OleObjectType::Link, hdxf::OleObjectType::Embedded, hdxf::OleObjectType::Static,
                                     static_cast<hdxf::OleObjectType>(9)}) {
        for (int paper : {0, 1}) {
            hdxf::Drawing d(DxfVersion::R2018);
            hdxf::Entity e = make_ole2(pattern(40), kind, paper);
            e.as_ole2_frame().upper_left = {-10.5, 20.25, 3.0};
            e.as_ole2_frame().lower_right = {30.0, -5.0, 3.0};
            e.as_ole2_frame().application_name = std::string("Applikation ") + "\xC3\xA5\xC3\xA4\xC3\xB6" + std::string(300, 'x');
            d.add_entity(e);
            std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Binary);
            assert_true(!out.empty(), d.last_error());
            hdxf::Drawing r;
            assert_true(r.load_from_memory(out.data(), out.size()), r.last_error());
            const hdxf::Entity* back = first_entity(r, hdxf::EntityType::Ole2Frame);
            assert_true(back != nullptr, "read back");
            const hdxf::Ole2FrameData& f = back->as_ole2_frame();
            assert_true(f.object_type == kind, "71 as it is");
            assert_eq(paper, back->props.paper_space, "67");
            assert_eq(paper, static_cast<int>(f.tile_mode), "72");
            assert_eq(std::string("Applikation \xC3\xA5\xC3\xA4\xC3\xB6") + std::string(300, 'x'), f.application_name, "3, UTF-8");
            assert_eq(-10.5, f.upper_left.x, "10");
            assert_eq(-5.0, f.lower_right.y, "21 of 11");
        }
    }
}

TEST(ole_proxy_ole2frame_class_is_in_classes_once_from_2000_and_in_r14) {
    for (DxfVersion version : {DxfVersion::R14, DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2013}) {
        hdxf::Drawing d(version);
        d.add_entity(make_ole2(pattern(10)));
        d.add_entity(make_ole2(pattern(11)));
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), d.last_error());
        hdxf::Drawing r;
        assert_true(r.load_from_memory(out.data(), out.size()), r.last_error());
        int found = 0;
        for (const ClassRow& c : classes_of(r)) {
            if (c.dxf_name != "OLE2FRAME") continue;
            ++found;
            assert_eq(std::string("AcDbOle2Frame"), c.cpp_name, "the C++ class");
            assert_true(c.entity, "an entity class");
            if (version >= DxfVersion::R2004) assert_eq(2, c.count, "two frames");
        }
        assert_eq(1, found, std::string(version_name(version)) + ": one CLASS OLE2FRAME");
    }
}

TEST(ole_proxy_a_malformed_ole_frame_stays_as_read_with_a_warning) {
    for (const char* kind : {"OLE2FRAME", "OLEFRAME"}) {
        hdxf::Drawing d(DxfVersion::R2010);
        d.add_entity(std::string(kind) == "OLE2FRAME" ? make_ole2(pattern(300)) : make_oleframe(pattern(300)));
        std::vector<uint8_t> good = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!good.empty(), d.last_error());
        const std::string text(good.begin(), good.end());
        struct Mutation {
            const char* what;
            std::function<std::string(std::string)> change;
        };
        const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        auto replace_once = [](std::string s, const std::string& from, const std::string& to) {
            size_t at = s.find(from);
            assert_true(at != std::string::npos, "the pattern is in the file: " + from);
            s.replace(at, from.size(), to);
            return s;
        };
        // the group 90 of the OLE data: the line after the subclass's last " 90"
        auto length_group = [&](std::string s, const std::string& value) {
            // the subclass marker (100), not the C++ class name of CLASSES
            size_t sub = s.find(eol + "100" + eol + "AcDb" + (std::string(kind) == "OLE2FRAME" ? "Ole2Frame" : "OleFrame") + eol);
            assert_true(sub != std::string::npos, "the subclass");
            size_t at = s.find(eol + " 90" + eol, sub);
            assert_true(at != std::string::npos, "the 90 group");
            size_t from = at + eol.size() + 3 + eol.size();
            size_t to = s.find(eol, from);
            s.replace(from, to - from, value);
            return s;
        };
        const Mutation mutations[] = {
            {"a byte count that is not the bytes", [&](std::string s) { return length_group(s, "     301"); }},
            {"a terminator that is not OLE", [&](std::string s) { return replace_once(s, eol + "OLE" + eol, eol + "OLX" + eol); }},
            {"no terminator", [&](std::string s) { return replace_once(s, "  1" + eol + "OLE" + eol, ""); }},
            {"an odd number of hex digits", [&](std::string s) {
                 size_t at = s.find(eol + "310" + eol);
                 assert_true(at != std::string::npos, "a 310");
                 size_t from = at + eol.size() + 3 + eol.size();
                 return s.erase(from, 1);
             }},
            {"a negative byte count", [&](std::string s) { return length_group(s, "      -5"); }},
        };
        for (const Mutation& m : mutations) {
            std::string mutated = m.change(text);
            std::vector<uint8_t> bytes(mutated.begin(), mutated.end());
            hdxf::Drawing r;
            assert_true(r.load_from_memory(bytes.data(), bytes.size()), std::string(kind) + " " + m.what + ": " + r.last_error());
            int untyped = 0;
            for (const auto& e : r.entities()) {
                if (e.type == hdxf::EntityType::Unknown && e.as_unknown().entity_type == kind) ++untyped;
            }
            assert_eq(1, untyped, std::string(kind) + " " + m.what + ": kept untyped");
            bool warned = false;
            for (const auto& w : r.warnings()) warned = warned || (w.find(kind) != std::string::npos && w.find("kept as it was read") != std::string::npos);
            assert_true(warned, std::string(kind) + " " + m.what + ": a warning");
            std::vector<uint8_t> out = r.save_to_memory(DxfFormat::Ascii);
            assert_true(out == bytes, std::string(kind) + " " + m.what + ": written back as read, " + first_difference(bytes, out));
        }
    }
}

TEST(ole_proxy_a_read_ole_frame_is_written_back_unchanged_and_a_changed_payload_as_a_unit) {
    for (DxfVersion version : {DxfVersion::R14, DxfVersion::R2000, DxfVersion::R2007, DxfVersion::R2018}) {
        for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
            hdxf::Drawing d(version);
            d.add_entity(make_ole2(pattern(700)));
            std::vector<uint8_t> first = d.save_to_memory(format);
            assert_true(!first.empty(), d.last_error());
            hdxf::Drawing r;
            assert_true(r.load_from_memory(first.data(), first.size()), r.last_error());
            std::vector<uint8_t> again = r.save_to_memory(format);
            assert_true(again == first, std::string(version_name(version)) + ": unchanged, " + first_difference(first, again));
            // the payload shorter by one byte: 90 and the 310 groups follow, the other fields are as they were
            hdxf::Ole2FrameData& f = entities_of(r, hdxf::EntityType::Ole2Frame)[0]->as_ole2_frame();
            f.data.pop_back();
            f.object_type = hdxf::OleObjectType::Link;
            std::vector<uint8_t> changed = r.save_to_memory(format);
            assert_true(!changed.empty(), r.last_error());
            hdxf::Drawing back;
            assert_true(back.load_from_memory(changed.data(), changed.size()), back.last_error());
            const hdxf::Ole2FrameData& g = first_entity(back, hdxf::EntityType::Ole2Frame)->as_ole2_frame();
            assert_eq(699, static_cast<int>(g.data.size()), "the payload, one byte shorter");
            assert_true(g.object_type == hdxf::OleObjectType::Link, "71 changed");
            assert_eq(std::string("Paintbrush Picture"), g.application_name, "3 as it was");
        }
    }
}

TEST(ole_proxy_a_read_ole_frame_with_its_raw_emptied_is_written_anew_to_the_same_model) {
    for (DxfVersion version : {DxfVersion::R14, DxfVersion::R2010}) {
        hdxf::Drawing d(version);
        d.add_entity(make_ole2(pattern(333)));
        std::vector<uint8_t> first = d.save_to_memory(DxfFormat::Ascii);
        hdxf::Drawing r;
        assert_true(r.load_from_memory(first.data(), first.size()), r.last_error());
        entities_of(r, hdxf::EntityType::Ole2Frame)[0]->raw = hdxf::RawRecord();
        std::vector<uint8_t> anew = without_version_guid(r.save_to_memory(DxfFormat::Ascii));
        assert_true(!anew.empty(), r.last_error());
        first = without_version_guid(first);
        assert_true(anew == first, std::string(version_name(version)) + ": written anew like the first time, " + first_difference(first, anew));
    }
}

TEST(ole_proxy_an_ole_frames_own_proxy_graphics_are_kept_and_follow_proxygraphics) {
    for (bool enabled : {true, false}) {
        hdxf::Drawing d(DxfVersion::R2010);
        d.set_header_int("$PROXYGRAPHICS", 70, enabled ? 1 : 0);
        hdxf::Entity e = make_ole2(pattern(50));
        e.as_ole2_frame().proxy_graphics = pattern(520, 4);
        d.add_entity(e);
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Binary);
        assert_true(!out.empty(), d.last_error());
        hdxf::Drawing r;
        assert_true(r.load_from_memory(out.data(), out.size()), r.last_error());
        const hdxf::Entity* back = first_entity(r, hdxf::EntityType::Ole2Frame);
        assert_eq(enabled ? 520 : 0, static_cast<int>(back->as_ole2_frame().proxy_graphics.size()), enabled ? "kept" : "PROXYGRAPHICS 0");
        assert_true(back->as_ole2_frame().data == pattern(50), "the payload");
    }
    // R14 has no proxy graphics of an entity (they are 2000's)
    hdxf::Drawing d(DxfVersion::R14);
    hdxf::Entity e = make_ole2(pattern(50));
    e.as_ole2_frame().proxy_graphics = pattern(100, 4);
    d.add_entity(e);
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), d.last_error());
    hdxf::Drawing r;
    assert_true(r.load_from_memory(out.data(), out.size()), r.last_error());
    assert_eq(0, static_cast<int>(first_entity(r, hdxf::EntityType::Ole2Frame)->as_ole2_frame().proxy_graphics.size()), "none in R14");
}

TEST(ole_proxy_ole_frames_in_a_block_and_removing_them_leaves_the_rest) {
    hdxf::Drawing d(DxfVersion::R2013);
    hdxf::Block& block = d.add_block("WITHOLE");
    block.entities.push_back(make_ole2(pattern(64)));
    d.add_entity(make_ole2(pattern(65)));
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), d.last_error());
    hdxf::Drawing r;
    assert_true(r.load_from_memory(out.data(), out.size()), r.last_error());
    assert_eq(2, static_cast<int>(entities_of(r, hdxf::EntityType::Ole2Frame).size()), "one in the block, one in the model");
    std::vector<uint8_t> again = r.save_to_memory(DxfFormat::Ascii);
    assert_true(again == out, "unchanged: " + first_difference(out, again));
    // the model space frame removed
    r.entities().erase(std::remove_if(r.entities().begin(), r.entities().end(),
                                      [](const hdxf::Entity& e) { return e.type == hdxf::EntityType::Ole2Frame; }),
                       r.entities().end());
    std::vector<uint8_t> fewer = r.save_to_memory(DxfFormat::Ascii);
    assert_true(!fewer.empty(), r.last_error());
    hdxf::Drawing last;
    assert_true(last.load_from_memory(fewer.data(), fewer.size()), last.last_error());
    assert_eq(1, static_cast<int>(entities_of(last, hdxf::EntityType::Ole2Frame).size()), "the block's frame is left");
}

// ---------------------------------------------------------------------------
// More of the proxies: the four pointer kinds, a proxy entity written and read again
// ---------------------------------------------------------------------------

TEST(ole_proxy_all_four_pointer_kinds_are_kept_in_order_and_removed_targets_become_null_ids_by_place) {
    for (DxfVersion version : {DxfVersion::R14, DxfVersion::R2000, DxfVersion::R2007, DxfVersion::R2018}) {
        for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const std::string where = std::string(version_name(version)) + " " + hdxf_test::format_label(format);
            hdxf::Drawing d(version);
            for (const char* h : {"FF01", "FF02", "FF03", "FF04"}) {
                hdxf::Object g;
                g.type = hdxf::ObjectType::Group;
                g.handle = h;
                g.owner = "0";
                g.data = hdxf::GroupData{};
                d.objects().push_back(g);
            }
            hdxf::Object o = make_proxy_object("FF10", "POINTERS", 16, {1, 2}, version);
            auto& refs = o.as_proxy_object().proxy.references;
            refs.push_back({hdxf::ProxyPointerKind::SoftPointer, "FF01"});
            refs.push_back({hdxf::ProxyPointerKind::HardPointer, "FF02"});
            refs.push_back({hdxf::ProxyPointerKind::SoftOwner, "FF03"});
            refs.push_back({hdxf::ProxyPointerKind::HardOwner, "FF04"});
            refs.push_back({hdxf::ProxyPointerKind::HardPointer, "0"});
            d.objects().push_back(o);
            std::vector<uint8_t> out = d.save_to_memory(format);
            assert_true(!out.empty(), where + ": " + d.last_error());
            hdxf::Drawing r;
            assert_true(r.load_from_memory(out.data(), out.size()), where + ": " + r.last_error());
            const hdxf::Object* back = r.find_object("FF10");
            assert_true(back != nullptr && back->type == hdxf::ObjectType::ProxyObject, where + ": the proxy");
            const auto& got = back->as_proxy_object().proxy.references;
            assert_eq(5, static_cast<int>(got.size()), where + ": five ids");
            const hdxf::ProxyPointerKind kinds[5] = {hdxf::ProxyPointerKind::SoftPointer, hdxf::ProxyPointerKind::HardPointer,
                                                     hdxf::ProxyPointerKind::SoftOwner, hdxf::ProxyPointerKind::HardOwner,
                                                     hdxf::ProxyPointerKind::HardPointer};
            const char* targets[5] = {"FF01", "FF02", "FF03", "FF04", "0"};
            for (size_t i = 0; i < 5; ++i) {
                assert_true(got[i].kind == kinds[i], where + ": kind " + std::to_string(i));
                assert_eq(std::string(targets[i]), got[i].handle, where + ": target " + std::to_string(i));
            }
            // unchanged: the same bytes
            assert_true(r.save_to_memory(format) == out, where + ": unchanged");
            // two of the targets removed from the drawing as read: their ids are 0 in place, the others stay
            auto& objects = r.objects();
            objects.erase(std::remove_if(objects.begin(), objects.end(),
                                         [](const hdxf::Object& x) { return x.handle == "FF02" || x.handle == "FF04"; }),
                          objects.end());
            std::vector<uint8_t> fewer = r.save_to_memory(format);
            assert_true(!fewer.empty(), where + ": " + r.last_error());
            int warned = 0;
            for (const auto& w : r.warnings()) warned += w.find("FF02") != std::string::npos || w.find("FF04") != std::string::npos;
            assert_eq(2, warned, where + ": a warning for each");
            hdxf::Drawing last;
            assert_true(last.load_from_memory(fewer.data(), fewer.size()), last.last_error());
            const auto& after = last.find_object("FF10")->as_proxy_object().proxy.references;
            assert_eq(5, static_cast<int>(after.size()), where + ": the id list keeps its places");
            const char* expected[5] = {"FF01", "0", "FF03", "0", "0"};
            for (size_t i = 0; i < 5; ++i) {
                assert_true(after[i].kind == kinds[i], where + ": kind of " + std::to_string(i) + " is kept");
                assert_eq(std::string(expected[i]), after[i].handle, where + ": id " + std::to_string(i));
            }
        }
    }
}

TEST(ole_proxy_a_proxy_entity_read_from_a_file_is_written_back_unchanged_and_from_its_typed_values) {
    for (DxfVersion version : {DxfVersion::R14, DxfVersion::R2000, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2018}) {
        for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const std::string where = std::string(version_name(version)) + " " + hdxf_test::format_label(format);
            hdxf::Drawing d(version);
            hdxf::Entity e = make_proxy_entity("ROUNDTRIP");
            if (version >= DxfVersion::R2007) {
                e.as_proxy_entity().proxy.graphics = pattern(130, 6);
                e.as_proxy_entity().proxy.graphics_bits = 130 * 8 - 5;
            }
            if (version >= DxfVersion::R2000) e.as_proxy_entity().entity_graphics = pattern(90, 8);
            d.add_entity(e);
            d.add_entity(make_proxy_entity("ROUNDTRIP"));
            std::vector<uint8_t> first = d.save_to_memory(format);
            assert_true(!first.empty(), where + ": " + d.last_error());
            hdxf::Drawing r;
            assert_true(r.load_from_memory(first.data(), first.size()), where + ": " + r.last_error());
            assert_eq(2, static_cast<int>(entities_of(r, hdxf::EntityType::ProxyEntity).size()), where + ": two typed proxy entities");
            assert_true(r.save_to_memory(format) == first, where + ": unchanged");
            for (hdxf::Entity* p : entities_of(r, hdxf::EntityType::ProxyEntity)) p->raw = hdxf::RawRecord();
            std::vector<uint8_t> anew = without_version_guid(r.save_to_memory(format));
            assert_true(anew == without_version_guid(first), where + ": written anew like the first time, " +
                                                                first_difference(without_version_guid(first), anew));
            // a changed byte of the class data rewrites the class data and leaves the graphics
            hdxf::Drawing c;
            assert_true(c.load_from_memory(first.data(), first.size()), c.last_error());
            entities_of(c, hdxf::EntityType::ProxyEntity)[0]->as_proxy_entity().proxy.data[3] ^= 0x55;
            std::vector<uint8_t> changed = c.save_to_memory(format);
            assert_true(!changed.empty(), where + ": " + c.last_error());
            hdxf::Drawing back;
            assert_true(back.load_from_memory(changed.data(), changed.size()), back.last_error());
            const hdxf::ProxyEntityData& pe = first_entity(back, hdxf::EntityType::ProxyEntity)->as_proxy_entity();
            assert_eq(static_cast<int>(pattern(21, 5)[3] ^ 0x55), static_cast<int>(pe.proxy.data[3]), where + ": the changed byte");
            if (version >= DxfVersion::R2000) assert_true(pe.entity_graphics == pattern(90, 8), where + ": the entity's own graphics stay");
            if (version >= DxfVersion::R2007) assert_true(pe.proxy.graphics == pattern(130, 6), where + ": the class's graphics stay");
        }
    }
}

TEST(ole_proxy_the_registry_has_the_four_types_with_their_versions_and_the_autocad_axis_stated) {
    const TypedRegistry& registry = TypedRegistry::instance();
    for (const char* name : {"OLEFRAME", "OLE2FRAME", "ACAD_PROXY_ENTITY", "ACAD_PROXY_OBJECT"}) {
        assert_true(registry.implemented(name), std::string(name) + " has a codec");
        const TypeRegistryEntry* entry = registry.find_entry(name);
        assert_true(entry != nullptr && entry->step == 3, std::string(name) + " belongs to step 9.3");
    }
    // the proxy object: AutoCAD saved it in 2000 … 2018 and saved an R12 file without it
    const ObjectCodec* proxy = registry.find_object("ACAD_PROXY_OBJECT");
    for (DxfVersion v : hdxf_test::all_versions) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const VersionCell& cell = proxy->versions.at(v, f);
            if (v == DxfVersion::R13 || v == DxfVersion::R14) {
                assert_true(cell.autocad == AutoCadStatus::NotSavable, "AutoCAD 2025 cannot save R13 or R14");
            } else {
                assert_true(cell.autocad == AutoCadStatus::Verified, std::string("AutoCAD saved ") + hdxf_test::version_label(v));
            }
        }
    }
    assert_true(proxy->versions.at(DxfVersion::R12, DxfFormat::Ascii).basis == ErrorBasis::Documented, "R12: a documented error");
    assert_true(proxy->versions.at(DxfVersion::R13, DxfFormat::Ascii).basis == ErrorBasis::NotEstablished, "R13: not established");
    // R13's proxy object cannot be built (no form is established) but a drawing read from an R13 file keeps its own
    hdxf::Drawing d(DxfVersion::R13);
    d.objects().push_back(make_proxy_object("FFF1", "R13OBJ", 8, {1}, DxfVersion::R13));
    assert_true(d.save_to_memory(DxfFormat::Ascii).empty(), "built for R13: refused");
    assert_true(d.last_error().find("cannot be built for DXF R13") != std::string::npos, d.last_error());
}

// ---------------------------------------------------------------------------
// Step 9.S (Codex 5): the proxies of DXF data — AutoCAD's own form (70 = 1, the class's subclasses after the proxy's), typed
// through the class's codec. The AutoCAD files: doc/facit/acad_<version>_dxf_ext_*.dxf (fas9_extensions.scr: TurboCAD's,
// ZWCAD's and Map 3D's drawings saved by AutoCAD 2025's core) and acad_*_dxf_proxy*.dxf (gh209_1)
// ---------------------------------------------------------------------------

namespace {

bool is_binary(const std::vector<uint8_t>& bytes) {
    static const std::string head = "AutoCAD Binary DXF";
    return bytes.size() > head.size() && std::equal(head.begin(), head.end(), bytes.begin());
}

// The files of the corpus that have a proxy record: AutoCAD's (doc/facit) and the third-party originals they were saved from
std::vector<std::filesystem::path> proxy_corpus() {
    std::vector<std::filesystem::path> out;
    auto scan = [&](const std::filesystem::path& dir, bool autocad) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) return;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            const std::string name = hdxf::utf8_from_path(entry.path().filename());
            if (entry.path().extension() != ".dxf") continue;
            if (autocad && (name.rfind("acad_", 0) != 0 ||
                            (name.find("_dxf_ext_") == std::string::npos && name.find("_dxf_proxy") == std::string::npos))) {
                continue;
            }
            std::vector<uint8_t> bytes = read_bytes(entry.path());
            static const std::string key = "ACAD_PROXY_";
            if (std::search(bytes.begin(), bytes.end(), key.begin(), key.end()) == bytes.end()) continue;
            out.push_back(entry.path());
        }
    };
    scan(hdxf_test::source_path("doc/facit"), true);
    scan(hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / "local-extensions", false);
    std::sort(out.begin(), out.end());
    return out;
}

struct ProxyCount {
    int objects = 0;            // typed ACAD_PROXY_OBJECT
    int entities = 0;           // typed ACAD_PROXY_ENTITY
    int with_class = 0;         // of them, with their class's data typed (70 = 1)
    int untyped = 0;
};

template <typename F>
void each_proxy_entity(hdxf::Drawing& d, F&& f) {
    for (auto& e : d.entities()) f(e);
    for (auto& b : d.blocks()) for (auto& e : b.entities) f(e);
}

ProxyCount count_proxies(hdxf::Drawing& d) {
    ProxyCount c;
    for (const auto& o : d.objects()) {
        if (o.type == hdxf::ObjectType::ProxyObject) {
            ++c.objects;
            if (o.as_proxy_object().class_data) ++c.with_class;
        } else if (o.type == hdxf::ObjectType::Unknown && o.as_unknown().object_type == "ACAD_PROXY_OBJECT") {
            ++c.untyped;
        }
    }
    each_proxy_entity(d, [&](hdxf::Entity& e) {
        if (e.type == hdxf::EntityType::ProxyEntity) {
            ++c.entities;
            if (e.as_proxy_entity().class_data) ++c.with_class;
        } else if (e.type == hdxf::EntityType::Unknown && e.as_unknown().entity_type == "ACAD_PROXY_ENTITY") {
            ++c.untyped;
        }
    });
    return c;
}

// handle → the preserved groups of every typed proxy
std::map<std::string, std::vector<std::pair<int, std::string>>> proxy_records(hdxf::Drawing& d) {
    std::map<std::string, std::vector<std::pair<int, std::string>>> out;
    for (auto& o : d.objects()) if (o.type == hdxf::ObjectType::ProxyObject) out[o.handle] = groups_of(o.raw);
    each_proxy_entity(d, [&](hdxf::Entity& e) {
        if (e.type == hdxf::EntityType::ProxyEntity) out[e.props.handle] = groups_of(e.raw);
    });
    return out;
}

// handle -> the typed values of every proxy, as text (the class's data by its type and the groups its record has)
std::map<std::string, std::string> proxy_values(hdxf::Drawing& d) {
    std::map<std::string, std::string> out;
    auto text = [](const hdxf::ProxyClassData& p) {
        std::string s = std::to_string(p.class_id) + "|" + std::to_string(p.application_class_id) + "|" + p.application_class.dxf_name + "|" +
                        std::to_string(p.original_format) + "|" + std::to_string(p.data_bits) + "|" + std::to_string(p.graphics_bits) + "|";
        for (uint8_t b : p.data) s += std::to_string(b) + ",";
        s += "|";
        for (uint8_t b : p.graphics) s += std::to_string(b) + ",";
        for (const auto& r : p.references) s += "|" + std::to_string(static_cast<int>(r.kind)) + ":" + r.handle;
        if (p.drawing_format) s += "|" + std::to_string(p.drawing_format->combined());
        return s;
    };
    for (auto& o : d.objects()) {
        if (o.type != hdxf::ObjectType::ProxyObject) continue;
        const auto& p = o.as_proxy_object();
        out[o.handle] = text(p.proxy) + (p.class_data ? "|class " + std::to_string(static_cast<int>(p.class_data->type)) : std::string());
    }
    each_proxy_entity(d, [&](hdxf::Entity& e) {
        if (e.type != hdxf::EntityType::ProxyEntity) return;
        const auto& p = e.as_proxy_entity();
        out[e.props.handle] = text(p.proxy) + "|" + std::to_string(p.entity_graphics.size()) +
                              (p.class_data ? "|class " + std::to_string(static_cast<int>(p.class_data->type)) : std::string());
    });
    return out;
}

void empty_proxy_records(hdxf::Drawing& d) {
    for (auto& o : d.objects()) if (o.type == hdxf::ObjectType::ProxyObject) o.raw = hdxf::RawRecord();
    each_proxy_entity(d, [&](hdxf::Entity& e) {
        if (e.type == hdxf::EntityType::ProxyEntity) e.raw = hdxf::RawRecord();
    });
}

// The groups of an ASCII file's record of `type` with handle `handle` (the first record of the type when `handle` is empty),
// values trimmed
std::vector<std::pair<int, std::string>> ascii_record(const std::vector<uint8_t>& bytes, const std::string& type, const std::string& handle) {
    Lines file(bytes);
    for (size_t i = 0; i + 3 < file.lines.size(); i += 2) {
        if (Lines::trim(file.lines[i]) != "0" || Lines::trim(file.lines[i + 1]) != type) continue;
        if (!handle.empty() && (Lines::trim(file.lines[i + 2]) != "5" || Lines::trim(file.lines[i + 3]) != handle)) continue;
        std::vector<std::pair<int, std::string>> out;
        out.push_back({0, type});
        for (size_t k = i + 2; k + 1 < file.lines.size() && Lines::trim(file.lines[k]) != "0"; k += 2) {
            out.push_back({std::atoi(file.lines[k].c_str()), Lines::trim(file.lines[k + 1])});
        }
        return out;
    }
    return {};
}

// The groups from the subclass marker `from` on, without `skip_code` (91: the class's place in CLASSES differs)
std::vector<std::pair<int, std::string>> from_marker(const std::vector<std::pair<int, std::string>>& record, const std::string& from,
                                                     int skip_code = -1) {
    std::vector<std::pair<int, std::string>> out;
    bool on = false;
    for (const auto& g : record) {
        if (g.first == 100 && g.second == from) on = true;
        if (on && g.first != skip_code) out.push_back(g);
    }
    return out;
}


// A real's last digit: HDxf writes the shortest form that reads back as the same double, AutoCAD 16 digits rounded its own way
// ("76.66666412353515" and "76.66666412353516" are the same double); the tolerance of the 9.18 tests
bool is_real_code(int code) {
    return (code >= 10 && code <= 59) || (code >= 110 && code <= 149) || (code >= 210 && code <= 239) || (code >= 460 && code <= 469) ||
           (code >= 1010 && code <= 1059);
}

// A string as written in a 2000/2004 file of code page 1252: its characters, a byte above 0x7F as its code point (1252 is
// Latin-1 from 0xA0) and \U+XXXX as the code point. HDxf writes text anew in 2000 and 2004 with everything outside ASCII as
// \U+XXXX (B16); AutoCAD's ZWM proxy class data has the byte (acad_2000_dxf_ext_zwm: 0xD8, "Ø")
std::vector<uint32_t> characters_1252(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s.compare(i, 3, "\\U+") == 0 && i + 7 <= s.size()) {
            out.push_back(static_cast<uint32_t>(std::strtoul(s.substr(i + 3, 4).c_str(), nullptr, 16)));
            i += 6;
        } else {
            out.push_back(static_cast<unsigned char>(s[i]));
        }
    }
    return out;
}

bool same_group(const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) {
    if (a == b) return true;
    if (a.first != b.first) return false;
    if (!is_real_code(a.first)) {
        auto high = [](uint32_t c) { return c >= 0xA0 && c <= 0xFF; };
        const std::vector<uint32_t> x = characters_1252(a.second), y = characters_1252(b.second);
        if (x != y) return false;
        bool any = false;
        for (uint32_t c : x) any = any || high(c);
        return any;
    }
    const double p = std::atof(a.second.c_str()), q = std::atof(b.second.c_str());
    return std::fabs(p - q) <= 1e-12 * std::max(1.0, std::fabs(p));
}

std::string first_group_difference(const std::vector<std::pair<int, std::string>>& a, const std::vector<std::pair<int, std::string>>& b) {
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        if (!same_group(a[i], b[i])) {
            return "group " + std::to_string(i) + ": " + std::to_string(a[i].first) + " \"" + a[i].second + "\" and " +
                   std::to_string(b[i].first) + " \"" + b[i].second + "\"";
        }
    }
    if (a.size() != b.size()) return std::to_string(a.size()) + " and " + std::to_string(b.size()) + " groups";
    return {};
}

hdxf::Drawing load_bytes(const std::vector<uint8_t>& bytes, const std::string& what) {
    hdxf::Drawing d;
    assert_true(d.load_from_memory(bytes.data(), bytes.size()), what + ": " + d.last_error());
    return d;
}

hdxf::Entity* proxy_entity(hdxf::Drawing& d, const std::string& handle) {
    hdxf::Entity* found = nullptr;
    each_proxy_entity(d, [&](hdxf::Entity& e) {
        if (e.props.handle == handle && e.type == hdxf::EntityType::ProxyEntity) found = &e;
    });
    return found;
}

} // namespace

TEST(ole_proxy_every_proxy_of_the_corpus_is_typed_and_written_back_unchanged_and_from_its_typed_values) {
    const std::vector<std::filesystem::path> files = proxy_corpus();
    if (files.empty()) hdxf_test::skip("no file with a proxy (doc/facit/acad_*_dxf_ext_*, acad_*_dxf_proxy* are gitignored)");
    int total = 0, with_class = 0, objects = 0, entities = 0;
    for (const auto& path : files) {
        const std::string name = hdxf::utf8_from_path(path.filename());
        const std::vector<uint8_t> in = read_bytes(path);
        const DxfFormat format = is_binary(in) ? DxfFormat::Binary : DxfFormat::Ascii;
        hdxf::Drawing d = load_bytes(in, name);
        const ProxyCount c = count_proxies(d);
        // every proxy is typed: the class of each one of DXF data is a type HDxf has (TurboCAD's, Map's, ZWM's)
        assert_eq(0, c.untyped, name + ": untyped proxies");
        objects += c.objects;
        entities += c.entities;
        total += c.objects + c.entities;
        with_class += c.with_class;
        // B12: unchanged, the same bytes
        std::vector<uint8_t> out = d.save_to_memory(format);
        assert_true(out == in, name + ": unchanged, " + first_difference(in, out));
        // the typed values alone: AutoCAD's own record, group for group
        const auto before = proxy_records(d);
        empty_proxy_records(d);
        std::vector<uint8_t> anew = d.save_to_memory(format);
        assert_true(!anew.empty(), name + ": " + d.last_error());
        hdxf::Drawing again = load_bytes(anew, name + " anew");
        assert_eq(0, count_proxies(again).untyped, name + ": untyped after writing anew");
        const auto after = proxy_records(again);
        assert_eq(static_cast<int>(before.size()), static_cast<int>(after.size()), name + ": the proxies written anew");
        // (a third-party original is written anew in AutoCAD's form - its head, the 94 TurboCAD leaves out -: the same values)
        const bool autocad = name.rfind("acad_", 0) == 0;
        const auto values_before = proxy_values(d), values_after = proxy_values(again);
        for (const auto& [handle, groups] : before) {
            auto it = after.find(handle);
            assert_true(it != after.end(), name + " " + handle + ": written anew");
            if (autocad) {
                const std::string why = first_group_difference(groups, it->second);
                assert_true(why.empty(), name + " " + handle + ": written anew as AutoCAD wrote it, " + why);
            } else {
                assert_true(values_after.at(handle) == values_before.at(handle), name + " " + handle + ": written anew with its values");
            }
        }
        // the other format holds the same: every proxy typed again, with its class's data where it had it
        const DxfFormat other = format == DxfFormat::Ascii ? DxfFormat::Binary : DxfFormat::Ascii;
        std::vector<uint8_t> converted = again.save_to_memory(other);
        assert_true(!converted.empty(), name + ": " + again.last_error());
        hdxf::Drawing third = load_bytes(converted, name + " other format");
        const ProxyCount t = count_proxies(third);
        assert_true(t.untyped == 0 && t.objects == c.objects && t.entities == c.entities && t.with_class == c.with_class,
                    name + ": the same proxies in the other format");
    }
    std::cout << "  proxies of the corpus: " << files.size() << " files, " << total << " proxies (" << objects << " objects, "
              << entities << " entities), " << with_class << " of DXF data typed through their class\n";
    assert_true(with_class > 0, "the proxies of DXF data are in the corpus");
}

TEST(ole_proxy_the_turbocad_point_of_dxf_data_is_read_in_the_92_and_160_forms) {
    // AutoCAD's 1FC: the point (-4.5, -9, 0), TcPointEntity 70 = 1, 71 = 4, 40 = 2; 92 in 2000/2004, 160 from 2010
    int checked = 0;
    for (const char* tag : {"2000", "2004", "2010", "2013", "2018"}) {
        for (bool binary : {false, true}) {
            const std::string name = std::string("acad_") + tag + "_dxf_ext_tc_echocal_blockdiag" + (binary ? "_bin" : "") + ".dxf";
            std::vector<uint8_t> in = read_bytes(facit(name));
            if (in.empty()) continue;
            ++checked;
            hdxf::Drawing d = load_bytes(in, name);
            hdxf::Entity* e = proxy_entity(d, "1FC");
            assert_true(e != nullptr, name + ": 1FC is a typed proxy entity");
            const hdxf::ProxyEntityData& p = e->as_proxy_entity();
            assert_eq(1, static_cast<int>(p.proxy.original_format), name + ": 70");
            assert_true(p.proxy.data.empty() && p.proxy.references.empty(), name + ": no DWG stream, no ids");
            assert_eq(2584, static_cast<int>(p.proxy.graphics.size()), name + ": the class's graphics (92/160)");
            assert_eq(2584 * 8, static_cast<int>(p.proxy.graphics_bits), name + ": counted in bytes");
            assert_true(p.proxy.graphics == p.entity_graphics, name + ": the same as the entity's own");
            assert_true(p.proxy.drawing_format && p.proxy.drawing_format->dwg_version == 27 &&
                            p.proxy.drawing_format->maintenance_version == 25, name + ": 95 / 71 97");
            assert_eq(std::string("TCPOINTENTITY"), p.proxy.application_class.dxf_name, name + ": the class");
            assert_true(static_cast<bool>(p.class_data), name + ": the class's data");
            assert_true(p.class_data->type == hdxf::EntityType::TcPoint, name + ": as TurboCAD's point");
            const hdxf::TcPointData& t = *std::get<hdxf::Boxed<hdxf::TcPointData>>(p.class_data->data);
            assert_true(t.point.location.x == -4.5 && t.point.location.y == -9.0 && t.point.location.z == 0.0, name + ": AcDbPoint");
            assert_eq(1, static_cast<int>(t.version), name + ": TcPointEntity 70");
            assert_eq(4, static_cast<int>(t.flags), name + ": 71");
            assert_true(t.size == 2.0, name + ": 40");
            // the extents are the symbol's, as the point's own
            hdxf::Point3 lo, hi;
            assert_true(d.entity_extents(*e, lo, hi) && lo.x <= -4.5 && hi.x >= -4.5, name + ": the symbol's extents");
        }
    }
    if (checked == 0) hdxf_test::skip("doc/facit/acad_*_dxf_ext_tc_echocal_blockdiag*.dxf is not here");
}

TEST(ole_proxy_a_change_of_the_proxy_part_or_the_class_data_loses_nothing_of_the_other) {
    int checked = 0;
    for (const char* tag : {"2000", "2010", "2018"}) {
        for (bool binary : {false, true}) {
            const std::string name = std::string("acad_") + tag + "_dxf_ext_tc_echocal_blockdiag" + (binary ? "_bin" : "") + ".dxf";
            std::vector<uint8_t> in = read_bytes(facit(name));
            if (in.empty()) continue;
            ++checked;
            const DxfFormat format = binary ? DxfFormat::Binary : DxfFormat::Ascii;
            hdxf::Drawing d = load_bytes(in, name);
            const auto before = proxy_records(d);
            // the proxy's part of one point (a byte of its graphics), the class's data of another (its size)
            hdxf::Entity* a = proxy_entity(d, "1FC");
            assert_true(a != nullptr, name + ": 1FC");
            a->as_proxy_entity().proxy.graphics[40] ^= 0x01;
            std::string other_handle;
            each_proxy_entity(d, [&](hdxf::Entity& e) {
                if (e.type == hdxf::EntityType::ProxyEntity && e.props.handle != "1FC" && other_handle.empty()) other_handle = e.props.handle;
            });
            hdxf::Entity* b = proxy_entity(d, other_handle);
            (*std::get<hdxf::Boxed<hdxf::TcPointData>>(b->as_proxy_entity().class_data->data)).size = 3.5;
            // and an object's proxy part (its maintenance release)
            std::string object_handle;
            for (auto& o : d.objects()) {
                if (o.type == hdxf::ObjectType::ProxyObject && o.as_proxy_object().class_data && object_handle.empty()) {
                    object_handle = o.handle;
                    o.as_proxy_object().proxy.drawing_format->maintenance_version = 26;
                }
            }
            std::vector<uint8_t> out = d.save_to_memory(format);
            assert_true(!out.empty(), name + ": " + d.last_error());
            hdxf::Drawing r = load_bytes(out, name + " changed");
            assert_eq(0, count_proxies(r).untyped, name + ": untyped after the change");
            const auto after = proxy_records(r);
            // 1FC: one group differs (a 310 of the proxy's graphics); its class's subclasses are as they were
            auto differing = [](const std::vector<std::pair<int, std::string>>& x, const std::vector<std::pair<int, std::string>>& y) {
                int n = 0;
                for (size_t i = 0; i < std::min(x.size(), y.size()); ++i) n += x[i] != y[i];
                return n + static_cast<int>(std::max(x.size(), y.size()) - std::min(x.size(), y.size()));
            };
            assert_eq(1, differing(before.at("1FC"), after.at("1FC")), name + ": 1FC differs in one group");
            assert_true(from_marker(before.at("1FC"), "AcDbPoint") == from_marker(after.at("1FC"), "AcDbPoint"),
                        name + ": 1FC's AcDbPoint and TcPointEntity stay");
            const hdxf::TcPointData& t = *std::get<hdxf::Boxed<hdxf::TcPointData>>(proxy_entity(r, "1FC")->as_proxy_entity().class_data->data);
            assert_true(t.size == 2.0 && t.point.location.x == -4.5, name + ": 1FC's class data");
            // the other point: its 40 differs, its proxy part is as it was
            assert_eq(1, differing(before.at(other_handle), after.at(other_handle)), name + ": " + other_handle + " differs in one group");
            assert_true(from_marker(before.at(other_handle), "AcDbProxyEntity").size() > 6, name + ": the proxy part");
            const hdxf::Entity* changed = proxy_entity(r, other_handle);
            assert_true((*std::get<hdxf::Boxed<hdxf::TcPointData>>(changed->as_proxy_entity().class_data->data)).size == 3.5,
                        name + ": the changed size");
            // the object: 95 (71/97) differs, TcOptionalData stays
            assert_true(!object_handle.empty(), name + ": a proxy object of DXF data");
            assert_eq(1, differing(before.at(object_handle), after.at(object_handle)), name + ": " + object_handle + " differs in one group");
            assert_true(from_marker(before.at(object_handle), "TcOptionalData") == from_marker(after.at(object_handle), "TcOptionalData"),
                        name + ": " + object_handle + "'s TcOptionalData stays");
        }
    }
    if (checked == 0) hdxf_test::skip("doc/facit/acad_*_dxf_ext_tc_echocal_blockdiag*.dxf is not here");
}

TEST(ole_proxy_a_proxy_whose_class_data_cannot_be_typed_whole_stays_as_read_with_a_warning) {
    std::vector<uint8_t> in = read_bytes(facit("acad_2010_dxf_ext_tc_echocal_blockdiag.dxf"));
    if (in.empty()) hdxf_test::skip("doc/facit/acad_2010_dxf_ext_tc_echocal_blockdiag.dxf is not here");
    struct Case {
        const char* what;
        std::function<void(Lines&)> change;
        int untyped;            // proxy entities that stay untyped
        const char* warning;
    };
    const Case cases[] = {
        {"a class HDxf has no type for",
         [](Lines& f) {
             for (auto& l : f.lines) if (Lines::trim(l) == "TCPOINTENTITY") l = "TCXPOINTENTITY";
         },
         6, "is not a type HDxf has"},
        {"a group TurboCAD's point does not have",
         [](Lines& f) {
             size_t at = f.find("ACAD_PROXY_ENTITY", "1FC", "40");
             f.lines.insert(f.lines.begin() + static_cast<long>(at) + 2, {" 41", "3.0"});
         },
         1, "not held whole"},
        {"the class's subclasses in a proxy of DWG data (70 = 0)",
         [](Lines& f) {
             size_t at = f.find("ACAD_PROXY_ENTITY", "1FC", "70");
             f.lines[at + 1] = "     0";
         },
         1, "is not a form AutoCAD writes"},
        {"a group in the proxy's subclass that none of its forms has",
         [](Lines& f) {
             size_t at = f.find("ACAD_PROXY_ENTITY", "1FC", "70");
             f.lines.insert(f.lines.begin() + static_cast<long>(at) + 2, {"  1", "later"});
         },
         1, "the proxy's forms do not have"},
    };
    for (const Case& c : cases) {
        Lines file(in);
        c.change(file);
        std::vector<uint8_t> bytes = file.bytes();
        hdxf::Drawing d = load_bytes(bytes, c.what);
        const ProxyCount count = count_proxies(d);
        assert_eq(c.untyped, count.untyped, std::string(c.what) + ": untyped");
        bool warned = false;
        for (const auto& w : d.warnings()) warned = warned || (w.find(c.warning) != std::string::npos && w.find("kept as it was read") != std::string::npos);
        assert_true(warned, std::string(c.what) + ": a warning says \"" + c.warning + "\"");
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(out == bytes, std::string(c.what) + ": written back as it was read, " + first_difference(bytes, out));
    }
}

TEST(ole_proxy_a_new_proxy_of_dxf_data_is_autocads_record_in_every_version_and_its_class_in_its_own) {
    std::vector<uint8_t> source = read_bytes(facit("acad_2010_dxf_ext_tc_echocal_blockdiag.dxf"));
    if (source.empty()) hdxf_test::skip("doc/facit/acad_2010_dxf_ext_tc_echocal_blockdiag.dxf is not here");
    hdxf::Drawing src = load_bytes(source, "echocal 2010");
    const hdxf::Entity* point = proxy_entity(src, "1FC");
    assert_true(point != nullptr, "1FC");
    // TCOPTIONALDATA's first proxy of DXF data
    const hdxf::Object* option = nullptr;
    for (const auto& o : src.objects()) {
        if (!option && o.type == hdxf::ObjectType::ProxyObject && o.as_proxy_object().class_data &&
            o.as_proxy_object().class_data->type == hdxf::ObjectType::TcOptionalData) {
            option = &o;
        }
    }
    assert_true(option != nullptr, "a TCOPTIONALDATA proxy");
    const std::string option_handle = option->handle;
    int compared = 0;
    for (DxfVersion v : hdxf_test::all_versions) {
        for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const std::string where = std::string(version_name(v)) + " " + hdxf_test::format_label(format);
            hdxf::Drawing d(v);
            hdxf::Entity e = *point;
            e.raw = hdxf::RawRecord();
            e.props.handle.clear();
            e.props.layer = "0";
            e.props.reactors.clear();
            e.props.xdictionary.clear();
            d.add_entity(e);
            hdxf::Object o = *option;
            o.raw = hdxf::RawRecord();
            o.handle = "FFF0";
            o.owner = "0";
            o.reactors.clear();
            o.xdictionary.clear();
            if (v >= DxfVersion::R2000) d.objects().push_back(o);
            std::vector<uint8_t> out = d.save_to_memory(format);
            if (v < DxfVersion::R14) {
                assert_true(out.empty(), where + ": no proxy entity before R14");
                assert_eq(std::string("ACAD_PROXY_ENTITY requires DXF R14 or later"), d.last_error(), where);
                continue;
            }
            if (v == DxfVersion::R14) {
                // no file shows a proxy of DXF data in R14 (AutoCAD 2025 cannot save it): refused, named
                assert_true(out.empty(), where + ": refused");
                assert_true(d.last_error().find("not established") != std::string::npos, where + ": " + d.last_error());
                continue;
            }
            assert_true(!out.empty(), where + ": " + d.last_error());
            hdxf::Drawing r = load_bytes(out, where);
            if (v == DxfVersion::R2007) {
                // the version the point and the option were made in (95's 27): their own records, as AutoCAD writes them
                int points = 0, options = 0;
                for (const auto& x : r.entities()) points += x.type == hdxf::EntityType::TcPoint;
                for (const auto& x : r.objects()) options += x.type == hdxf::ObjectType::TcOptionalData;
                assert_eq(1, points, where + ": a TCPOINTENTITY");
                assert_eq(1, options, where + ": a TCOPTIONALDATA");
                assert_eq(0, count_proxies(r).entities + count_proxies(r).objects, where + ": no proxy");
                if (format == DxfFormat::Ascii) {
                    std::vector<uint8_t> autocad = read_bytes(facit("acad_2007_dxf_ext_tc_echocal_blockdiag.dxf"));
                    if (!autocad.empty()) {
                        auto want = from_marker(ascii_record(autocad, "TCPOINTENTITY", "1FC"), "AcDbPoint");
                        auto got = from_marker(ascii_record(out, "TCPOINTENTITY", ""), "AcDbPoint");
                        assert_true(!want.empty() && first_group_difference(want, got).empty(), where + ": AutoCAD's TCPOINTENTITY, " + first_group_difference(want, got));
                        want = from_marker(ascii_record(autocad, "TCOPTIONALDATA", option_handle), "TcOptionalData");
                        got = from_marker(ascii_record(out, "TCOPTIONALDATA", "FFF0"), "TcOptionalData");
                        assert_true(!want.empty() && first_group_difference(want, got).empty(), where + ": AutoCAD's TCOPTIONALDATA, " + first_group_difference(want, got));
                        ++compared;
                    }
                }
                continue;
            }
            const ProxyCount c = count_proxies(r);
            assert_true(c.entities == 1 && c.objects == 1 && c.with_class == 2 && c.untyped == 0, where + ": the two proxies of DXF data");
            if (format == DxfFormat::Ascii && v != DxfVersion::R2007) {
                const std::string tag = version_name(v);
                std::vector<uint8_t> autocad = read_bytes(facit("acad_" + tag + "_dxf_ext_tc_echocal_blockdiag.dxf"));
                if (autocad.empty()) continue;
                // AutoCAD's record from the proxy's subclass on (91 is the class's place in CLASSES, which differs)
                auto want = from_marker(ascii_record(autocad, "ACAD_PROXY_ENTITY", "1FC"), "AcDbProxyEntity", 91);
                auto got = from_marker(ascii_record(out, "ACAD_PROXY_ENTITY", ""), "AcDbProxyEntity", 91);
                assert_true(!want.empty() && first_group_difference(want, got).empty(), where + ": AutoCAD's ACAD_PROXY_ENTITY, " + first_group_difference(want, got));
                want = from_marker(ascii_record(autocad, "ACAD_PROXY_OBJECT", option_handle), "AcDbProxyObject", 91);
                got = from_marker(ascii_record(out, "ACAD_PROXY_OBJECT", "FFF0"), "AcDbProxyObject", 91);
                assert_true(!want.empty() && first_group_difference(want, got).empty(), where + ": AutoCAD's ACAD_PROXY_OBJECT, " + first_group_difference(want, got));
                ++compared;
            }
        }
    }
    std::cout << "  new proxies of DXF data compared with AutoCAD's records: " << compared << " versions\n";
}
