#include "test_framework.hpp"
#include "type_version_matrix.hpp"
#include "typed_corpus.hpp"
#include "dxf_structure.hpp"
#include "hdxf.hpp"
#include "TypedRecordTypes.hpp"
#include "StyleTypes.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Step 9.7 of phase 9 (plan 2026-10-27-fas-9-typer.md): MATERIAL, the three forms of VISUALSTYLE and what uses them.
//
// The AutoCAD originals are doc/facit/acad_<version>_dxf_materials*.dxf and acad_<version>_dxf_visual_styles*.dxf (made by
// doc/facit/fas9_material_styles.scr with AutoCAD 2025's core from AutoCAD's own DXF of 21_native_records.scr, which has the
// material HDXF_SIX_MAPS with six maps and non-identity mapper transforms), acad_<version>_dxf_native_records[_material_changed]
// (the only pair with a material changed: opacity added), ACadSharp's samples and jscad's empty2007/2010/2013 (build/test-data).
// They are local files (the facit is not in git): a test whose file is missing is skipped.
// ---------------------------------------------------------------------------

using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::MaterialData;
using hdxf::Object;
using hdxf::ObjectType;
using hdxf::VisualStyleData;
using hdxf::VisualStyleEntry;
using hdxf::VisualStyleForm;
using hdxf::VisualStyleProperty;
using hdxf::VisualStyleValueKind;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;
using namespace hdxf::internal;

namespace {

using Group = std::pair<int, std::string>;
using Rec = std::vector<Group>;

const char* const kTags[] = {"2000", "2004", "2007", "2010", "2013", "2018"};
const DxfVersion kVersions[] = {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007,
                                DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018};

std::filesystem::path facit_path(const std::string& name) { return hdxf_test::source_path("doc/facit/" + name); }

std::vector<uint8_t> facit_bytes(const std::string& name) {
    std::vector<uint8_t> in = hdxf_test::read_file_bytes(facit_path(name));
    if (in.empty()) hdxf_test::skip("missing doc/facit/" + name);
    return in;
}

std::string facit_name(const char* tag, const std::string& theme, bool binary) {
    return std::string("acad_") + tag + "_dxf_" + theme + (binary ? "_bin" : "") + ".dxf";
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

std::vector<Object*> objects_of(Drawing& d, ObjectType type) {
    std::vector<Object*> out;
    for (Object& o : d.objects()) if (o.type == type) out.push_back(&o);
    return out;
}

// An independent reader of an ASCII file: every record as its groups (code, value), the 0 first
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

std::string trim(std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

const Rec* record_with_handle(const std::vector<Rec>& all, const std::string& handle) {
    for (const Rec& r : all) {
        for (const Group& g : r) {
            if (g.first == 5) {
                if (trim(g.second) == handle) return &r;
                break;
            }
        }
    }
    return nullptr;
}

// The groups of a record after its subclass marker `subclass` and up to the next marker or the XDATA
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

// Two groups with the same code and the same value, a number compared as a number (AutoCAD's 16 digits and ours)
bool same_group(const Group& a, const Group& b) {
    if (a.first != b.first) return false;
    std::string x = trim(a.second), y = trim(b.second);
    if (x == y) return true;
    char* ex = nullptr;
    char* ey = nullptr;
    double dx = std::strtod(x.c_str(), &ex);
    double dy = std::strtod(y.c_str(), &ey);
    return !x.empty() && !y.empty() && *ex == '\0' && *ey == '\0' && dx == dy;
}

std::string why_different(const std::vector<Group>& a, const std::vector<Group>& b) {
    if (a.size() != b.size()) return "the number of groups: " + std::to_string(a.size()) + " and " + std::to_string(b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        if (!same_group(a[i], b[i])) {
            return "group " + std::to_string(i) + ": " + std::to_string(a[i].first) + "=" + a[i].second + " and " +
                   std::to_string(b[i].first) + "=" + b[i].second;
        }
    }
    return {};
}

// A record as an ASCII DXF has it
std::string record_text(const Rec& r) {
    std::ostringstream s;
    for (const Group& g : r) {
        s << (g.first < 10 ? "  " : (g.first < 100 ? " " : "")) << g.first << "\n" << g.second << "\n";
    }
    return s.str();
}

// A drawing as 2000 with `records` added to its OBJECTS section, as text
std::string drawing_with_objects(const std::vector<Rec>& records, DxfVersion version = DxfVersion::R2000) {
    Drawing d(version);
    d.add_line({0, 0, 0}, {1, 1, 0});
    std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!bytes.empty(), d.last_error());
    std::string text = hdxf_test::lf_text(bytes);
    size_t at = text.rfind("  0\nENDSEC\n  0\nEOF");
    assert_true(at != std::string::npos, "the end of the file");
    std::string added;
    for (const Rec& r : records) added += record_text(r);
    return text.substr(0, at) + added + text.substr(at);
}

Drawing load_text(const std::string& text) {
    Drawing d;
    assert_true(d.load_from_memory(reinterpret_cast<const uint8_t*>(text.data()), text.size()), d.last_error());
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

// The file `out` points at nothing that `in` did not (a file AutoCAD saved has pointers of its own that lead nowhere or to
// records of other applications; what is removed must not add to them)
void expect_no_new_dangling_pointers(const std::vector<uint8_t>& in, const std::vector<uint8_t>& out, const std::string& what) {
    std::set<std::string> before = dxf_structure::dangling_pointers(dxf_structure::records_of(in));
    for (const std::string& d : dxf_structure::dangling_pointers(dxf_structure::records_of(out))) {
        assert_true(before.count(d) > 0, what + ": a pointer that leads nowhere: " + d);
    }
}

// Where two files first differ, with a little of the text around it
std::string first_difference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    size_t n = std::min(a.size(), b.size());
    size_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    auto around = [&](const std::vector<uint8_t>& v) {
        size_t from = i > 60 ? i - 60 : 0;
        std::string s;
        for (size_t k = from; k < v.size() && k < i + 60; ++k) s += v[k] == '\n' ? '|' : v[k] == '\r' ? '~' : static_cast<char>(v[k]);
        return s;
    };
    return "at byte " + std::to_string(i) + " (" + std::to_string(a.size()) + " and " + std::to_string(b.size()) + " bytes): [" + around(a) + "] against [" + around(b) + "]";
}

const Object* material_named(const Drawing& d, const std::string& name) {
    for (const Object& o : d.objects()) if (o.type == ObjectType::Material && o.as_material().name == name) return &o;
    return nullptr;
}

Object* material_named(Drawing& d, const std::string& name) {
    for (Object& o : d.objects()) if (o.type == ObjectType::Material && o.as_material().name == name) return &o;
    return nullptr;
}

Object* style_named(Drawing& d, const std::string& name) {
    for (Object& o : d.objects()) if (o.type == ObjectType::VisualStyle && o.as_visual_style().description == name) return &o;
    return nullptr;
}

const Object* style_named(const Drawing& d, const std::string& name) {
    for (const Object& o : d.objects()) if (o.type == ObjectType::VisualStyle && o.as_visual_style().description == name) return &o;
    return nullptr;
}

int count_type(const Drawing& d, ObjectType t) {
    int n = 0;
    for (const Object& o : d.objects()) n += o.type == t ? 1 : 0;
    return n;
}

// The dictionary ACAD_VISUALSTYLE (a style's name is the key of its entry; the style's own 2 is a description)
Object* visual_style_dictionary(Drawing& d) {
    for (const Object& o : d.objects()) {
        if (o.type != ObjectType::Dictionary || o.owner != "0") continue;
        for (const hdxf::DictionaryEntry& e : o.as_dictionary().entries) {
            if (e.name == "ACAD_VISUALSTYLE") return d.find_object(e.handle);
        }
    }
    return nullptr;
}

// What a record holds as one text, to compare two readings of the same drawing (the typed values in file order)
template <typename T>
void put(std::ostringstream& s, const char* name, const std::optional<T>& v) {
    if (v) s << name << "=" << *v << ";";
}

void describe_map(std::ostringstream& s, const char* name, const hdxf::MaterialMap& m) {
    s << "[" << name << ":";
    put(s, "blend", m.blend);
    put(s, "source", m.source);
    put(s, "file", m.file_name);
    put(s, "proj", m.projection);
    put(s, "tile", m.tiling);
    put(s, "auto", m.auto_transform);
    if (m.transform) {
        s << "t=";
        for (double v : *m.transform) s << v << ",";
    }
    s << "]";
}

void describe_color(std::ostringstream& s, const char* name, const hdxf::MaterialColor& c) {
    s << "[" << name << ":";
    put(s, "method", c.method);
    put(s, "factor", c.factor);
    put(s, "color", c.color);
    s << "]";
}

std::string describe(const MaterialData& m) {
    std::ostringstream s;
    s.precision(17);
    s << m.name << ";";
    put(s, "desc", m.description);
    describe_color(s, "ambient", m.ambient);
    describe_color(s, "diffuse", m.diffuse);
    describe_map(s, "dmap", m.diffuse_map);
    put(s, "gloss", m.gloss);
    describe_color(s, "specular", m.specular);
    describe_map(s, "smap", m.specular_map);
    describe_map(s, "rmap", m.reflection_map);
    put(s, "opacity", m.opacity);
    describe_map(s, "omap", m.opacity_map);
    describe_map(s, "bmap", m.bump_map);
    put(s, "ior", m.refraction_index);
    describe_map(s, "fmap", m.refraction_map);
    put(s, "bleed", m.color_bleed_scale);
    put(s, "indirect", m.indirect_dump_scale);
    put(s, "reflectance", m.reflectance_scale);
    put(s, "transmittance", m.transmittance_scale);
    put(s, "two", m.two_sided);
    put(s, "lum", m.luminance);
    put(s, "lummode", m.luminance_mode);
    put(s, "nmethod", m.normal_map_method);
    put(s, "nstrength", m.normal_map_strength);
    describe_map(s, "nmap", m.normal_map);
    put(s, "anon", m.anonymous);
    put(s, "gi", m.global_illumination_mode);
    put(s, "fg", m.final_gather_mode);
    for (const hdxf::MaterialProcedure& p : m.procedures) {
        s << "{proc " << p.name;
        for (const hdxf::MaterialProcedureValue& v : p.values) {
            switch (v.kind) {
                case hdxf::MaterialProcedureValue::Kind::Boolean: s << " b" << v.boolean; break;
                case hdxf::MaterialProcedureValue::Kind::Integer: s << " i" << v.integer; break;
                case hdxf::MaterialProcedureValue::Kind::Real:    s << " r" << v.real; break;
                case hdxf::MaterialProcedureValue::Kind::Text:    s << " t" << v.text; break;
                case hdxf::MaterialProcedureValue::Kind::Color:
                    s << " c" << v.color.aci << "/" << v.color.rgb << "/" << v.color.name;
                    break;
            }
        }
        put(s, " end", p.table_end);
        s << "}";
    }
    put(s, "utile", m.map_u_tile);
    put(s, "transl", m.translucence);
    put(s, "selfill", m.self_illumination);
    put(s, "refl", m.reflectivity);
    put(s, "model", m.illumination_model);
    put(s, "flags", m.channel_flags);
    for (const hdxf::DxfGroup& g : m.trailing) s << "+" << g.code << "=" << g.value;
    return s.str();
}

std::string describe(const VisualStyleData& d) {
    std::ostringstream s;
    s.precision(17);
    s << static_cast<int>(d.form) << ";" << d.description << ";" << d.style_type << ";";
    put(s, "version", d.format_version);
    s << "internal=" << d.internal_use << ";";
    put(s, "apply", d.edge_style_apply);
    put(s, "r45", d.trailing_real);
    for (const VisualStyleEntry& e : d.properties) {
        s << "(" << hdxf::visual_style_property_name(e.property) << "=";
        const hdxf::VisualStyleValue& v = e.value;
        switch (v.kind) {
            case VisualStyleValueKind::Integer: s << v.integer; break;
            case VisualStyleValueKind::Real:    s << v.real; break;
            case VisualStyleValueKind::Boolean: s << v.boolean; break;
            case VisualStyleValueKind::Text:    s << v.text; break;
            case VisualStyleValueKind::Color:
                s << v.color.aci << "/" << v.color.rgb;
                if (v.color_tag) s << "/tag" << *v.color_tag;
                break;
        }
        if (e.operation) s << " op" << *e.operation;
        s << ")";
    }
    for (int16_t op : d.extra_operations) s << "x" << op;
    for (const hdxf::DxfGroup& g : d.trailing) s << "+" << g.code << "=" << g.value;
    return s.str();
}

std::vector<std::string> describe_all(const Drawing& d, ObjectType type) {
    std::vector<std::string> out;
    for (const Object& o : d.objects()) {
        if (o.type != type) continue;
        out.push_back(o.handle + ": " + (type == ObjectType::Material ? describe(o.as_material()) : describe(o.as_visual_style())));
    }
    return out;
}

// The material and visual style records of an ASCII file by handle
std::map<std::string, Rec> records_of_types(const std::vector<Rec>& all, const std::string& type) {
    std::map<std::string, Rec> out;
    for (const Rec& r : all) {
        if (r.empty() || r[0].second != type) continue;
        for (const Group& g : r) {
            if (g.first == 5) {
                out[trim(g.second)] = r;
                break;
            }
        }
    }
    return out;
}

const std::array<double, 16> kSixMapsTransform = {2.0, 0.0, 0.0, 0.125, 0.0, 3.0, 0.0, 0.25, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0};

const hdxf::VisualStyleEntry& entry(const VisualStyleData& d, VisualStyleProperty p) {
    const VisualStyleEntry* e = d.find(p);
    assert_true(e != nullptr, std::string("the property ") + hdxf::visual_style_property_name(p));
    return *e;
}

} // namespace

// ---------------------------------------------------------------------------
// The codecs and their matrices
// ---------------------------------------------------------------------------

TEST(mvs_codecs_are_registered_with_their_matrices) {
    const TypedRegistry& registry = TypedRegistry::instance();
    const struct {
        const char* name;
        ObjectType type;
        const char* cpp;
        int flags;
    } cases[] = {{"MATERIAL", ObjectType::Material, "AcDbMaterial", 1153},
                 {"VISUALSTYLE", ObjectType::VisualStyle, "AcDbVisualStyle", 4095}};
    for (const auto& c : cases) {
        const ObjectCodec* codec = registry.find_object(c.name);
        assert_true(codec != nullptr, std::string(c.name) + ": a codec");
        assert_true(registry.implemented(c.name), std::string(c.name) + ": implemented");
        assert_true(codec->type == c.type && registry.find_object(c.type) == codec, std::string(c.name) + ": its type");
        assert_eq(std::string(codec->cls.cpp_name), std::string(c.cpp), std::string(c.name) + ": class");
        assert_eq(std::string(codec->cls.application), "ObjectDBX Classes", std::string(c.name) + ": application");
        assert_eq(codec->cls.proxy_flags, c.flags, std::string(c.name) + ": proxy flags");
        assert_false(codec->cls.in_older, std::string(c.name) + ": no class in R13/R14");
        assert_eq(static_cast<int>(codec->lists.size()), 1, std::string(c.name) + ": one list");
        assert_true(codec->lists[0].shape == ListShape::WholeSubclass, std::string(c.name) + ": the whole subclass is the list");
        // AutoCAD's 2000 … 2018 files have both; R12 has no OBJECTS section and R13/R14 no ACAD_MATERIAL/ACAD_VISUALSTYLE
        for (DxfVersion v : {DxfVersion::R12, DxfVersion::R13, DxfVersion::R14}) {
            for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
                const VersionCell& cell = codec->versions.at(v, f);
                assert_true(cell.generation == Generation::None && cell.basis == ErrorBasis::Documented,
                            std::string(c.name) + ": a named error before 2000");
            }
        }
        assert_eq(version_error_text(c.name, codec->versions, DxfVersion::R14), std::string(c.name) + " requires DXF 2000 or later",
                  "the named error");
        for (DxfVersion v : kVersions) {
            for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
                const VersionCell& cell = codec->versions.at(v, f);
                assert_true(cell.generation == Generation::Native, std::string(c.name) + ": native from 2000");
                assert_true(cell.autocad == AutoCadStatus::Verified, std::string(c.name) + ": AutoCAD's files have it");
            }
        }
    }
}

TEST(mvs_property_table_agrees_with_the_codes_of_the_older_forms) {
    // The 28 properties of the older forms: the group of the value, the kind, and the 58 of the newest form
    const int codes[28] = {71, 72, 73, 90, 40, 41, 63, 74, 91, 64, 65, 75, 175, 42, 92, 66, 43, 76, 77, 78, 67, 79, 170, 171, 290, 93, 44, 173};
    for (size_t id = 0; id < 28; ++id) {
        VisualStyleProperty p = static_cast<VisualStyleProperty>(id);
        assert_eq(hdxf::visual_style_property_code(p), codes[id], std::string("the code of ") + hdxf::visual_style_property_name(p));
    }
    for (size_t id = 28; id < hdxf::kVisualStylePropertyCount; ++id) {
        VisualStyleProperty p = static_cast<VisualStyleProperty>(id);
        assert_eq(hdxf::visual_style_property_code(p), 0, "the properties after 27 have no code of their own");
        assert_eq(std::string(hdxf::visual_style_property_name(p)), "Post2010Prop" + std::to_string(id), "AutoCAD's own name");
    }
    assert_true(hdxf::visual_style_property_kind(VisualStyleProperty::FaceMonoColor) == VisualStyleValueKind::Color, "a color");
    assert_true(hdxf::visual_style_property_kind(VisualStyleProperty::EdgeHidePrecision) == VisualStyleValueKind::Boolean, "a flag");
    assert_true(hdxf::visual_style_property_kind(VisualStyleProperty::Post2010Prop54) == VisualStyleValueKind::Text, "the one text");
    assert_true(hdxf::visual_style_property_kind(VisualStyleProperty::Post2010Prop41) == VisualStyleValueKind::Color, "a color");
}

// ---------------------------------------------------------------------------
// MATERIAL: AutoCAD's own files
// ---------------------------------------------------------------------------

TEST(mvs_material_autocad_files_are_read_field_by_field) {
    for (const char* tag : kTags) {
        std::vector<uint8_t> ascii_in = facit_bytes(facit_name(tag, "materials", false));
        std::vector<uint8_t> binary_in = facit_bytes(facit_name(tag, "materials", true));
        Drawing ascii = load(ascii_in, tag);
        Drawing binary = load(binary_in, std::string(tag) + " binary");
        const std::string where = tag;
        // Every MATERIAL is typed, with no warning about it
        for (const Object& o : ascii.objects()) assert_false(o.type == ObjectType::Unknown && o.as_unknown().object_type == "MATERIAL", where + ": untyped");
        assert_eq(static_cast<int>(objects_of(ascii, ObjectType::Material).size()), 4, where + ": four materials");
        assert_true(describe_all(ascii, ObjectType::Material) == describe_all(binary, ObjectType::Material), where + ": binary and ASCII agree");
        for (const std::string& w : ascii.warnings()) assert_true(w.find("MATERIAL") == std::string::npos, where + ": " + w);

        // HDXF_SIX_MAPS (made in the GUI by 21_native_records.scr, handle 24F)
        const Object* six = material_named(ascii, "HDXF_SIX_MAPS");
        assert_true(six != nullptr, where + ": HDXF_SIX_MAPS");
        const MaterialData& m = six->as_material();
        assert_eq(six->handle, "24F", where + ": its handle");
        assert_true(m.description && *m.description == "Six image maps; nonidentity mapper transforms", where + ": description");
        const hdxf::MaterialMap* maps[6] = {&m.diffuse_map, &m.specular_map, &m.reflection_map, &m.opacity_map, &m.bump_map, &m.refraction_map};
        for (int i = 0; i < 6; ++i) {
            assert_true(maps[i]->file_name.has_value() && maps[i]->file_name->find("raster.bmp") != std::string::npos,
                        where + ": file name of map " + std::to_string(i));
            assert_true(maps[i]->transform.has_value() && *maps[i]->transform == kSixMapsTransform, where + ": transform of map " + std::to_string(i));
            assert_false(maps[i]->blend || maps[i]->projection || maps[i]->tiling || maps[i]->auto_transform,
                         where + ": AutoCAD wrote no mapper values of map " + std::to_string(i));
            // Only the refraction map has its source (273 = 1): the default is not written for the others
            assert_eq(maps[i]->source.has_value() ? 1 : 0, i == 5 ? 1 : 0, where + ": the source of map " + std::to_string(i));
        }
        assert_eq(static_cast<int>(*m.refraction_map.source), 1, where + ": refraction source");
        assert_false(m.gloss || m.opacity || m.refraction_index || m.channel_flags || m.ambient.method || m.diffuse.color ||
                         m.specular.factor || m.normal_map.file_name || !m.procedures.empty() || !m.trailing.empty(),
                     where + ": nothing else in the record");

        // The standard materials: ByBlock and ByLayer are the name (and the channel flags from 2007), Global has four transforms
        for (const char* name : {"ByBlock", "ByLayer"}) {
            const Object* o = material_named(ascii, name);
            assert_true(o != nullptr, where + ": " + name);
            const MaterialData& s = o->as_material();
            bool flags = std::string(tag) != "2000" && std::string(tag) != "2004";
            assert_eq(s.channel_flags.has_value() ? 1 : 0, flags ? 1 : 0, where + ": " + name + " channel flags");
            if (flags) assert_eq(static_cast<int>(*s.channel_flags), 63, where + ": " + name + " flags 63");
            assert_false(s.description || s.diffuse_map.transform, where + ": " + name + " is short");
        }
        const MaterialData& global = material_named(ascii, "Global")->as_material();
        for (const hdxf::MaterialMap* map : {&global.diffuse_map, &global.reflection_map, &global.opacity_map, &global.bump_map}) {
            assert_true(map->transform.has_value(), where + ": Global's transform");
            assert_eq(static_cast<int>(std::lround((*map->transform)[0] * 1e6)), 20800, where + ": Global's scale");
            assert_eq(static_cast<int>(std::lround((*map->transform)[5] * 1e6)), 20800, where + ": Global's scale (row 2)");
            assert_eq(static_cast<int>((*map->transform)[10]), 1, where + ": Global's 1");
        }
        assert_false(global.specular_map.transform || global.refraction_map.transform, where + ": Global has no specular or refraction transform");
    }
}

TEST(mvs_material_forms_of_other_autocad_versions) {
    // ACadSharp's AutoCAD 2007 sample: ambient/diffuse/specular methods and colors (signed 32 bits), the maps with only their
    // matrices, 148 and 468, 93 and 94; the files of other producers' orders come in as typed values or as untyped records
    std::filesystem::path path = hdxf_test::source_path("build/test-data/acadsharp/sample_AC1021_ascii.dxf");
    std::vector<uint8_t> in = hdxf_test::read_file_bytes(path);
    if (in.empty()) hdxf_test::skip("no ACadSharp sample_AC1021");
    Drawing d = load(in, "sample_AC1021");
    int typed = 0;
    for (const Object& o : d.objects()) {
        if (o.type == ObjectType::Material) ++typed;
        assert_false(o.type == ObjectType::Unknown && o.as_unknown().object_type == "MATERIAL", "a material left untyped");
    }
    assert_true(typed >= 10, "the materials of the sample are typed: " + std::to_string(typed));
    const Object* tile = material_named(d, "1.5in Square - Medium Blue");
    assert_true(tile != nullptr, "a material with maps");
    const MaterialData& m = tile->as_material();
    assert_true(m.ambient.method && *m.ambient.method == 1 && m.ambient.color && *m.ambient.color == -1023410177, "ambient override");
    assert_true(m.diffuse.method && *m.diffuse.method == 1 && m.diffuse.color && *m.diffuse.color == -1023410177, "diffuse override");
    assert_true(m.diffuse_map.file_name && m.diffuse_map.file_name->find("Medium Blue.png") != std::string::npos, "diffuse map file");
    assert_true(m.diffuse_map.transform && std::fabs((*m.diffuse_map.transform)[0] - 0.1666666716337204) < 1e-15, "diffuse transform");
    assert_true(m.gloss && std::fabs(*m.gloss - 0.8500000238418579) < 1e-15, "gloss");
    assert_true(m.specular.method && *m.specular.method == 1 && m.specular.color && *m.specular.color == -1023410177, "specular");
    assert_true(m.specular_map.source && *m.specular_map.source == 0 && m.reflection_map.source && *m.reflection_map.source == 0 &&
                    m.opacity_map.source && *m.opacity_map.source == 0,
                "sources 77, 171 and 175 = 0 (no map)");
    assert_true(m.bump_map.blend && std::fabs(*m.bump_map.blend - 0.550000011920929) < 1e-15, "bump blend");
    assert_true(m.bump_map.file_name && m.bump_map.transform, "bump map and transform");
    assert_true(m.refraction_index && std::fabs(*m.refraction_index - 1.399999976158142) < 1e-15, "refraction index");
    assert_true(m.translucence && *m.translucence == 0.5 && m.reflectivity && std::fabs(*m.reflectivity - 0.0900000035762787) < 1e-15, "148 and 468");
    assert_true(m.channel_flags && *m.channel_flags == 17 && !m.illumination_model, "94 without 93");
    const MaterialData& metal = material_named(d, "Aluminum Cast")->as_material();
    assert_true(metal.illumination_model && *metal.illumination_model == 1 && metal.bump_map.source && *metal.bump_map.source == 0, "93 and 179");
    // The record as read is the same as the record written from its typed values
    Drawing copy = load(in, "again");
    for (Object& o : copy.objects()) if (o.type == ObjectType::Material) o.raw = hdxf::RawRecord();
    std::vector<uint8_t> out = save(copy, DxfFormat::Ascii, "materials rewritten");
    std::vector<Rec> before = ascii_records(in), after = ascii_records(out);
    std::map<std::string, Rec> a = records_of_types(before, "MATERIAL"), b = records_of_types(after, "MATERIAL");
    assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()), "the same materials");
    for (const auto& [handle, rec] : a) {
        auto it = b.find(handle);
        assert_true(it != b.end(), "material " + handle);
        std::string why = why_different(after_marker(rec, "AcDbMaterial"), after_marker(it->second, "AcDbMaterial"));
        assert_true(why.empty(), "material " + handle + " from typed values: " + why);
    }
}

TEST(mvs_material_is_written_from_typed_values_as_autocad_has_it) {
    for (const char* tag : kTags) {
        for (const char* theme : {"materials", "native_records"}) {
            std::vector<uint8_t> in = facit_bytes(facit_name(tag, theme, false));
            Drawing d = load(in, tag);
            for (Object& o : d.objects()) if (o.type == ObjectType::Material) o.raw = hdxf::RawRecord();
            std::vector<uint8_t> out = save(d, DxfFormat::Ascii, std::string(tag) + " " + theme);
            std::map<std::string, Rec> a = records_of_types(ascii_records(in), "MATERIAL");
            std::map<std::string, Rec> b = records_of_types(ascii_records(out), "MATERIAL");
            assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()), std::string(tag) + ": the same materials");
            for (const auto& [handle, rec] : a) {
                auto it = b.find(handle);
                assert_true(it != b.end(), std::string(tag) + ": material " + handle);
                std::string why = why_different(after_marker(rec, "AcDbMaterial"), after_marker(it->second, "AcDbMaterial"));
                assert_true(why.empty(), std::string(tag) + " " + theme + ": material " + handle + " written anew: " + why);
            }
            // Binary: written anew and read again, the same typed values
            std::vector<uint8_t> bin_in = facit_bytes(facit_name(tag, theme, true));
            Drawing binary = load(bin_in, tag);
            std::vector<std::string> was = describe_all(binary, ObjectType::Material);
            for (Object& o : binary.objects()) if (o.type == ObjectType::Material) o.raw = hdxf::RawRecord();
            std::vector<uint8_t> bout = save(binary, DxfFormat::Binary, std::string(tag) + " binary");
            Drawing back = load(bout, std::string(tag) + " binary again");
            assert_true(describe_all(back, ObjectType::Material) == was, std::string(tag) + ": binary written anew, read again");
        }
    }
}

TEST(mvs_material_unchanged_is_written_back_byte_for_byte) {
    for (const char* tag : kTags) {
        for (bool binary : {false, true}) {
            for (const char* theme : {"materials", "native_records_material_changed", "visual_styles"}) {
                std::vector<uint8_t> in = facit_bytes(facit_name(tag, theme, binary));
                Drawing d = load(in, tag);
                std::vector<uint8_t> out = save(d, binary ? DxfFormat::Binary : DxfFormat::Ascii, std::string(tag) + " " + theme);
                assert_true(out == in, std::string(tag) + " " + theme + (binary ? " binary" : " ASCII") + ": written back unchanged");
            }
        }
    }
}

TEST(mvs_material_changed_pair_from_autocad) {
    // AutoCAD's own pair: the six-map material before and after 140 (opacity) was added in the GUI. The typed change of the
    // first gives the record of the second; the other records are as they were
    for (const char* tag : kTags) {
        std::vector<uint8_t> before = facit_bytes(facit_name(tag, "native_records", false));
        std::vector<uint8_t> after = facit_bytes(facit_name(tag, "native_records_material_changed", false));
        Drawing a = load(before, tag);
        Drawing b = load(after, tag);
        const Object* six_a = material_named(a, "HDXF_SIX_MAPS");
        const Object* six_b = material_named(b, "HDXF_SIX_MAPS");
        assert_true(six_a && six_b, std::string(tag) + ": the material");
        assert_false(six_a->as_material().opacity.has_value(), std::string(tag) + ": no opacity before");
        assert_true(six_b->as_material().opacity && std::fabs(*six_b->as_material().opacity - 0.65) < 1e-12, std::string(tag) + ": opacity after");
        // Everything else typed is the same
        MaterialData expected = six_b->as_material();
        expected.opacity.reset();
        assert_eq(describe(expected), describe(six_a->as_material()), std::string(tag) + ": nothing else differs");

        material_named(a, "HDXF_SIX_MAPS")->as_material().opacity = 0.65;
        std::vector<uint8_t> out = save(a, DxfFormat::Ascii, tag);
        std::vector<Rec> out_records = ascii_records(out), after_records = ascii_records(after);
        const Rec* mine = record_with_handle(out_records, six_a->handle);
        const Rec* theirs = record_with_handle(after_records, six_b->handle);
        assert_true(mine && theirs, std::string(tag) + ": the records");
        std::string why = why_different(after_marker(*mine, "AcDbMaterial"), after_marker(*theirs, "AcDbMaterial"));
        assert_true(why.empty(), std::string(tag) + ": the changed record is AutoCAD's: " + why);
        // The other materials stay byte for byte as they were
        std::map<std::string, Rec> was = records_of_types(ascii_records(before), "MATERIAL");
        std::map<std::string, Rec> now = records_of_types(out_records, "MATERIAL");
        for (const auto& [handle, rec] : was) {
            if (handle == six_a->handle) continue;
            assert_true(rec == now[handle], std::string(tag) + ": material " + handle + " unchanged");
        }
    }
}

TEST(mvs_material_edits_scalars_optional_groups_and_transforms) {
    std::vector<uint8_t> in = facit_bytes(facit_name("2018", "materials", false));
    // An optional group the record does not have is inserted where AutoCAD puts it; one it has goes when removed
    {
        Drawing d = load(in, "2018");
        MaterialData& by_block = material_named(d, "ByBlock")->as_material();
        by_block.description = "an edited description";
        by_block.opacity = 0.25;
        by_block.refraction_index = 1.5;
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "edited");
        std::vector<Rec> recs = ascii_records(out);
        const Rec* r = record_with_handle(recs, "97");
        assert_true(r != nullptr, "ByBlock");
        std::vector<Group> g = after_marker(*r, "AcDbMaterial");
        const std::vector<Group> want = {{1, "ByBlock"}, {2, "an edited description"}, {140, "0.25"}, {145, "1.5"}, {94, "63"}};
        assert_true(why_different(g, want).empty(), "ByBlock edited: " + why_different(g, want));
        Drawing back = load(out, "edited");
        assert_true(material_named(back, "ByBlock")->as_material().description == std::optional<std::string>("an edited description"), "read back");
        // The other materials are as they were
        std::map<std::string, Rec> was = records_of_types(ascii_records(in), "MATERIAL"), now = records_of_types(recs, "MATERIAL");
        for (const auto& [handle, rec] : was) if (handle != "97") assert_true(rec == now[handle], "material " + handle + " unchanged");
        // Removing it again gives AutoCAD's record back, byte for byte the same file
        Drawing again = load(out, "edited");
        MaterialData& ab = material_named(again, "ByBlock")->as_material();
        ab.description.reset();
        ab.opacity.reset();
        ab.refraction_index.reset();
        std::vector<uint8_t> restored = save(again, DxfFormat::Ascii, "removed");
        std::map<std::string, Rec> original_records = records_of_types(ascii_records(in), "MATERIAL"), restored_records = records_of_types(ascii_records(restored), "MATERIAL");
        assert_true(original_records == restored_records, "the optional groups removed: AutoCAD's records (the file differs only in its $VERSIONGUID: " + first_difference(restored, in) + ")");
    }
    // A mapper transform (16 values): one that changes is written whole, in its place, with the maps after it as they were
    {
        Drawing d = load(in, "2018");
        MaterialData& six = material_named(d, "HDXF_SIX_MAPS")->as_material();
        (*six.specular_map.transform)[3] = 0.5;
        six.diffuse_map.projection = 2;
        six.bump_map.file_name.reset();
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "edited maps");
        Drawing back = load(out, "edited maps");
        const MaterialData& m = material_named(back, "HDXF_SIX_MAPS")->as_material();
        assert_eq(describe(m), describe(six), "the edited maps read back");
        std::vector<Rec> recs = ascii_records(out);
        const Rec* r = record_with_handle(recs, "24F");
        std::vector<Group> g = after_marker(*r, "AcDbMaterial");
        // 1, 2, 3, 73 (projection after the file), 43 x 16, 4 ...
        assert_eq(g[2].first, 3, "the diffuse file");
        assert_eq(g[3].first, 73, "the projection after the file");
        assert_eq(trim(g[3].second), "2", "the projection");
    }
}

TEST(mvs_material_new_record_without_a_template) {
    for (DxfVersion v : kVersions) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            Drawing d(v);
            d.add_circle({0, 0, 0}, 1.0);
            MaterialData m;
            m.name = "Brushed steel";
            m.description = "A new material with a long description: " + std::string(300, 'x');
            m.ambient.method = 1;
            m.ambient.factor = 0.75;
            m.ambient.color = -1023410177;
            m.diffuse.method = 1;
            m.diffuse.factor = 0.5;
            m.diffuse_map.file_name = "C:/maps/\xC3\xA5\xC3\xA4\xC3\xB6.png";     // UTF-8: å ä ö
            m.diffuse_map.blend = 0.9;
            m.diffuse_map.source = 1;
            m.diffuse_map.projection = 3;
            m.diffuse_map.tiling = 2;
            m.diffuse_map.auto_transform = 6;
            m.diffuse_map.transform = std::array<double, 16>{1, 0, 0, 5, 0, 1, 0, 6, 0, 0, 1, 7, 0, 0, 0, 1};
            m.gloss = 0.1;
            m.specular.method = 0;
            m.reflection_map.file_name = "env.hdr";
            m.reflection_map.projection = 4;
            m.opacity = 0.5;
            m.opacity_map.blend = 0.3;
            m.bump_map.source = 0;
            m.bump_map.transform = std::array<double, 16>{2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1};
            m.refraction_index = 1.33;
            m.refraction_map.file_name = "glass.png";
            m.channel_flags = 63;
            Object o = make_object(ObjectType::Material, "7C01", hdxf::Boxed<MaterialData>(m));
            d.objects().push_back(o);
            std::vector<uint8_t> out = save(d, f, std::string(hdxf::acadver_string(v)));
            Drawing back = load(out, "read back");
            const Object* r = material_named(back, "Brushed steel");
            assert_true(r != nullptr, std::string(hdxf::acadver_string(v)) + ": the material");
            assert_eq(describe(r->as_material()), describe(m), std::string(hdxf::acadver_string(v)) + ": typed values");
            assert_eq(r->handle, "7C01", "handle");
            // Its class once
            if (f == DxfFormat::Ascii) {
                int classes = 0;
                for (const Rec& rec : ascii_records(out)) {
                    if (rec.size() > 1 && rec[0].second == "CLASS" && rec[1].second == "MATERIAL") ++classes;
                }
                assert_eq(classes, 1, std::string(hdxf::acadver_string(v)) + ": the class MATERIAL once");
            }
            // And a second write of what was read is the same bytes (B12)
            assert_true(save(back, f, "again") == out, std::string(hdxf::acadver_string(v)) + ": written again unchanged");
        }
    }
}

TEST(mvs_material_modern_properties_and_procedures_follow_the_references_order) {
    // The 2007 tail, the normal map and the procedural tables are the reference's groups: no AutoCAD file has them (ej testat,
    // F9.6). The record is written in the reference's order and read back to the same values; the two formats agree
    MaterialData m;
    m.name = "Reference order";
    m.color_bleed_scale = 1.1;
    m.indirect_dump_scale = 1.2;
    m.reflectance_scale = 1.3;
    m.transmittance_scale = 1.4;
    m.two_sided = true;
    m.luminance = 150.0;
    m.luminance_mode = 1;
    m.normal_map_method = 2;
    m.normal_map_strength = 0.7;
    m.normal_map.file_name = "normal.png";
    m.normal_map.blend = 0.9;
    m.normal_map.source = 1;
    m.normal_map.projection = 2;
    m.normal_map.transform = std::array<double, 16>{3, 0, 0, 0, 0, 3, 0, 0, 0, 0, 3, 0, 0, 0, 0, 1};
    m.anonymous = true;
    m.global_illumination_mode = 1;
    m.final_gather_mode = 2;
    hdxf::MaterialProcedure p;
    p.name = "wood";
    hdxf::MaterialProcedureValue vb, vi, vr, vt, vc;
    vb.kind = hdxf::MaterialProcedureValue::Kind::Boolean;
    vb.boolean = true;
    vi.kind = hdxf::MaterialProcedureValue::Kind::Integer;
    vi.integer = 7;
    vr.kind = hdxf::MaterialProcedureValue::Kind::Real;
    vr.real = 0.125;
    vt.kind = hdxf::MaterialProcedureValue::Kind::Text;
    vt.text = "ring";
    vc.kind = hdxf::MaterialProcedureValue::Kind::Color;
    vc.color = hdxf::Color::from_rgb(10, 20, 30);
    p.values = {vb, vi, vr, vt, vc};
    p.table_end = true;
    m.procedures = {p};
    m.map_u_tile = 1;
    m.translucence = 0.3;
    m.self_illumination = 5;
    m.reflectivity = 0.2;
    m.illumination_model = 2;
    m.channel_flags = 63;
    for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
        Drawing d(DxfVersion::R2007);
        d.objects().push_back(make_object(ObjectType::Material, "7C02", hdxf::Boxed<MaterialData>(m)));
        std::vector<uint8_t> out = save(d, f, "modern material");
        Drawing back = load(out, "modern material");
        const Object* r = material_named(back, "Reference order");
        assert_true(r != nullptr, "the material");
        assert_eq(describe(r->as_material()), describe(m), "modern properties and procedure read back");
        if (f != DxfFormat::Ascii) continue;
        std::vector<Rec> recs = ascii_records(out);
        const Rec* rec = record_with_handle(recs, "7C02");
        std::vector<int> codes;
        for (const Group& g : after_marker(*rec, "AcDbMaterial")) codes.push_back(g.first);
        const std::vector<int> want = {1,   460, 461, 462, 463, 290, 464, 270, 271, 465, 42,  72,  3,   73, 43, 43, 43, 43, 43, 43, 43, 43,
                                       43,  43,  43,  43,  43,  43,  43,  43,  293, 272, 273, 300, 291, 271, 469, 301, 62, 420, 292, 270,
                                       148, 90,  468, 93,  94};
        // (the normal map has 42 72 3 73 and its 43 x 16 — 16 values, the two lists above hold them)
        std::vector<int> expected;
        for (int c : want) expected.push_back(c);
        // Count only the order of the first appearance of each section: the reference's, from 460 to 94
        auto first = [&](int code, size_t from) {
            for (size_t i = from; i < codes.size(); ++i) if (codes[i] == code) return static_cast<int>(i);
            return -1;
        };
        int at = first(460, 0);
        for (int code : {461, 462, 463, 290, 464, 270, 271, 465, 42, 72, 3, 73, 293, 272, 273, 300, 291}) {
            int next = first(code, static_cast<size_t>(at));
            assert_true(next >= at, "the reference's order: " + std::to_string(code));
            at = next;
        }
    }
}

TEST(mvs_material_malformed_records_are_kept_untyped_with_a_warning) {
    struct Case {
        const char* what;
        std::vector<Group> body;
        bool typed;
    };
    std::vector<Group> fifteen = {{1, "m"}};
    for (int i = 0; i < 15; ++i) fifteen.push_back({43, "1.0"});
    const std::vector<Case> cases = {
        {"a transform with 15 of its 16 values", fifteen, false},
        {"a value that is no number", {{1, "m"}, {44, "abc"}}, false},
        {"a value that does not fit int16", {{1, "m"}, {70, "99999"}}, false},
        {"no name", {{2, "only a description"}}, false},
        {"an integer that is no integer", {{1, "m"}, {94, "6.5"}}, false},
        {"groups after the ones HDxf knows (a later version's)", {{1, "m"}, {94, "63"}, {301, "later"}, {40, "1.0"}}, true},
    };
    for (const Case& c : cases) {
        Rec r = {{0, "MATERIAL"}, {5, "7D01"}, {330, "0"}, {100, "AcDbMaterial"}};
        r.insert(r.end(), c.body.begin(), c.body.end());
        std::string text = drawing_with_objects({r});
        Drawing d = load_text(text);
        const Object* o = d.find_object("7D01");
        assert_true(o != nullptr, std::string(c.what) + ": the record");
        bool typed = o->type == ObjectType::Material;
        assert_eq(typed ? 1 : 0, c.typed ? 1 : 0, std::string(c.what) + ": typed or not");
        bool warned = false;
        for (const std::string& w : d.warnings()) warned = warned || w.find("MATERIAL") != std::string::npos;
        assert_true(warned, std::string(c.what) + ": a warning that names the type");
        // The record is kept as it was
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, c.what);
        std::string out_text = hdxf_test::lf_text(out);
        assert_true(out_text.find(record_text(r)) != std::string::npos, std::string(c.what) + ": kept as read");
        if (typed) {
            // What a later version added stays with the typed record, also when it is written from the typed values
            Object* t = d.find_object("7D01") ? &*std::find_if(d.objects().begin(), d.objects().end(), [](const Object& x) { return x.handle == "7D01"; }) : nullptr;
            t->raw = hdxf::RawRecord();
            std::vector<uint8_t> again = save(d, DxfFormat::Ascii, "typed with trailing groups");
            std::vector<Rec> recs = ascii_records(again);
            const Rec* rec = record_with_handle(recs, "7D01");
            std::vector<Group> g = after_marker(*rec, "AcDbMaterial");
            assert_true(why_different(g, c.body).empty(), "trailing groups written back: " + why_different(g, c.body));
        }
    }
}

// ---------------------------------------------------------------------------
// VISUALSTYLE: AutoCAD's own files
// ---------------------------------------------------------------------------

TEST(mvs_visual_style_autocad_files_in_all_three_forms) {
    for (const char* tag : kTags) {
        std::vector<uint8_t> ascii_in = facit_bytes(facit_name(tag, "visual_styles", false));
        std::vector<uint8_t> binary_in = facit_bytes(facit_name(tag, "visual_styles", true));
        Drawing ascii = load(ascii_in, tag);
        Drawing binary = load(binary_in, std::string(tag) + " binary");
        const std::string where = tag;
        for (const Object& o : ascii.objects()) assert_false(o.type == ObjectType::Unknown && o.as_unknown().object_type == "VISUALSTYLE", where + ": untyped");
        assert_true(describe_all(ascii, ObjectType::VisualStyle) == describe_all(binary, ObjectType::VisualStyle), where + ": binary and ASCII agree");
        for (const std::string& w : ascii.warnings()) assert_true(w.find("VISUALSTYLE") == std::string::npos, where + ": " + w);
        const int styles = static_cast<int>(objects_of(ascii, ObjectType::VisualStyle).size());
        assert_eq(styles, 27, where + ": 24 standard styles (AutoCAD 2025) and the three of the script, typed: " + std::to_string(styles));

        const VisualStyleForm form = std::string(tag) == "2010" ? VisualStyleForm::Form2010
                                     : (std::string(tag) == "2013" || std::string(tag) == "2018") ? VisualStyleForm::Form2013
                                                                                                    : VisualStyleForm::Form2007;
        const bool true_color = std::string(tag) != "2000";
        for (const char* name : {"HDXF_FACES", "HDXF_EDGES", "HDXF_DISPLAY"}) {
            const Object* o = style_named(ascii, name);
            assert_true(o != nullptr, where + ": " + name);
            const VisualStyleData& s = o->as_visual_style();
            assert_true(s.form == form, where + ": " + name + " form");
            assert_eq(static_cast<int>(s.style_type), 10, where + ": " + name + " type");
            assert_true(!s.internal_use, where + ": internal flag");
            if (form == VisualStyleForm::Form2007) {
                assert_false(s.format_version.has_value(), where + ": no 177");
                assert_true(s.edge_style_apply && *s.edge_style_apply == 0 && s.trailing_real && *s.trailing_real == 0.0, where + ": 174 and 45");
            } else {
                assert_true(s.format_version && *s.format_version == (form == VisualStyleForm::Form2010 ? 2 : 3), where + ": 177");
                assert_false(s.edge_style_apply || s.trailing_real, where + ": no 174 or 45 after 2007");
            }
            // The 58 properties: the newest form has all in the record, the two older have 28 and the other 30 in the XRECORD
            assert_eq(static_cast<int>(s.properties.size()), 58, where + ": " + name + ": 58 properties (the XRECORD's read into the style)");
            for (size_t i = 0; i < s.properties.size(); ++i) {
                assert_eq(static_cast<int>(s.properties[i].property), static_cast<int>(i), where + ": the properties in order");
                assert_true(s.properties[i].value.kind == hdxf::visual_style_property_kind(s.properties[i].property), where + ": the kind");
                assert_true(s.properties[i].operation.has_value(), where + ": every property has its operation");
            }
            assert_eq(static_cast<int>(s.extra_operations.size()), form == VisualStyleForm::Form2007 ? 5 : 0, where + ": RTVSPropertyOp58 … only in the 2007 form");
        }
        // HDXF_FACES: what VSFACESTYLE ... VSLIGHTINGQUALITY asked for (the facit script)
        const VisualStyleData& faces = style_named(ascii, "HDXF_FACES")->as_visual_style();
        assert_eq(entry(faces, VisualStyleProperty::FaceLightingModel).value.integer, 3, where + ": Gooch");
        assert_eq(entry(faces, VisualStyleProperty::FaceLightingQuality).value.integer, 3, where + ": lighting quality");
        assert_eq(entry(faces, VisualStyleProperty::FaceColorMode).value.integer, 5, where + ": color mode");
        assert_eq(entry(faces, VisualStyleProperty::FaceModifiers).value.integer, 3, where + ": modifiers");
        assert_eq(entry(faces, VisualStyleProperty::FaceOpacity).value.real, 0.5, where + ": opacity");
        assert_eq(entry(faces, VisualStyleProperty::FaceSpecular).value.real, 20.0, where + ": highlight");
        const hdxf::VisualStyleValue& mono = entry(faces, VisualStyleProperty::FaceMonoColor).value;
        assert_eq(mono.color.aci, 22, where + ": mono color index");
        assert_eq(mono.color.rgb, true_color ? 13120050 : -1, where + ": mono color true color (RGB:200,50,50 is 0xC83232 = 13120050)");
        assert_eq(mono.color_tag.has_value() ? 1 : 0, form == VisualStyleForm::Form2007 ? 1 : 0, where + ": 62 before 63 only in the 2007 form");
        if (mono.color_tag) assert_eq(static_cast<int>(*mono.color_tag), 5, where + ": AutoCAD's 62 is always 5");
        // HDXF_EDGES: VSEDGES 2, VSEDGECOLOR RGB:0,0,255, VSEDGEJITTER 3, VSEDGEOVERHANG 6, VSEDGESMOOTH 60, the obscured and
        // intersection settings, VSSILHWIDTH 3, VSHALOGAP 5, VSHIDEPRECISION 1 — which property each is (the names)
        const VisualStyleData& edges = style_named(ascii, "HDXF_EDGES")->as_visual_style();
        assert_eq(entry(edges, VisualStyleProperty::EdgeModel).value.integer, 2, where + ": VSEDGES");
        assert_eq(entry(edges, VisualStyleProperty::EdgeIntersectionColor).value.color.aci, 2, where + ": VSINTERSECTIONCOLOR");
        assert_eq(entry(edges, VisualStyleProperty::EdgeObscuredColor).value.color.aci, 3, where + ": VSOBSCUREDCOLOR");
        assert_eq(entry(edges, VisualStyleProperty::EdgeObscuredLinetype).value.integer, 3, where + ": VSOBSCUREDLTYPE");
        assert_eq(entry(edges, VisualStyleProperty::EdgeIntersectionLinetype).value.integer, 2, where + ": VSINTERSECTIONLTYPE");
        assert_eq(entry(edges, VisualStyleProperty::EdgeCreaseAngle).value.real, 60.0, where + ": VSEDGESMOOTH");
        assert_eq(entry(edges, VisualStyleProperty::EdgeColor).value.color.aci, 5, where + ": VSEDGECOLOR");
        assert_eq(entry(edges, VisualStyleProperty::EdgeColor).value.color.rgb, true_color ? 255 : -1, where + ": VSEDGECOLOR RGB:0,0,255");
        assert_eq(entry(edges, VisualStyleProperty::EdgeOverhang).value.integer, 6, where + ": VSEDGEOVERHANG");
        assert_eq(entry(edges, VisualStyleProperty::EdgeJitter).value.integer, 3, where + ": VSEDGEJITTER");
        assert_eq(entry(edges, VisualStyleProperty::EdgeSilhouetteWidth).value.integer, 3, where + ": VSSILHWIDTH");
        assert_eq(entry(edges, VisualStyleProperty::EdgeHaloGap).value.integer, 5, where + ": VSHALOGAP");
        assert_true(entry(edges, VisualStyleProperty::EdgeHidePrecision).value.boolean, where + ": VSHIDEPRECISION");
        // The XRECORD's properties: AutoCAD's defaults (BlurAmount-like 50, the one text strokes_ogs.tif, the colors)
        assert_eq(entry(edges, VisualStyleProperty::Post2010Prop37).value.integer, 50, where + ": Post2010Prop37");
        assert_eq(entry(edges, VisualStyleProperty::Post2010Prop54).value.text, "strokes_ogs.tif", where + ": Post2010Prop54");
        assert_eq(entry(edges, VisualStyleProperty::Post2010Prop41).value.color.aci, 18, where + ": Post2010Prop41");
        assert_eq(entry(edges, VisualStyleProperty::Post2010Prop44).value.color.aci, 5, where + ": Post2010Prop44");
        assert_eq(entry(edges, VisualStyleProperty::Post2010Prop44).value.color.rgb, 255, where + ": Post2010Prop44 RGB");
        assert_eq(static_cast<int>(*entry(edges, VisualStyleProperty::Post2010Prop51).operation), 0, where + ": the operation of Post2010Prop51 is 0");
        assert_eq(static_cast<int>(*entry(edges, VisualStyleProperty::Post2010Prop57).operation), 1, where + ": the last property");
        assert_eq(static_cast<int>(*entry(edges, VisualStyleProperty::FaceLightingModel).operation), 1, where + ": the first property");
        // The viewport style (VPORT 348) and the saved view (VIEW 348) name the style by its handle
        const Object* current = style_named(ascii, "HDXF_FACES");
        const bool has_348 = std::string(tag) != "2000" && std::string(tag) != "2004";    // (348 is 2007's: no 2000 or 2004 file has it)
        bool view_found = false;
        for (const hdxf::View& v : ascii.views()) {
            if (v.name != "HDXF_VIEW") continue;
            view_found = true;
            assert_eq(v.visual_style_handle, has_348 ? current->handle : std::string(), where + ": VIEW 348 is HDXF_FACES from 2007");
        }
        assert_true(view_found, where + ": the view");
        bool vport = false;
        for (const hdxf::Vport& v : ascii.vports()) {
            if (dxf_structure::upper_copy_of(v.name) != "*ACTIVE") continue;
            vport = true;
            assert_eq(v.visual_style_handle, has_348 ? current->handle : std::string(), where + ": VPORT 348 is HDXF_FACES from 2007");
        }
        assert_true(vport, where + ": the viewport table record");
    }
}

TEST(mvs_visual_style_written_from_typed_values_is_autocads) {
    for (const char* tag : kTags) {
        for (const char* theme : {"visual_styles", "visual_styles_renamed", "visual_styles_viewport"}) {
            std::vector<uint8_t> in = facit_bytes(facit_name(tag, theme, false));
            Drawing d = load(in, tag);
            std::vector<std::string> was = describe_all(d, ObjectType::VisualStyle);
            for (Object& o : d.objects()) if (o.type == ObjectType::VisualStyle) o.raw = hdxf::RawRecord();
            std::vector<uint8_t> out = save(d, DxfFormat::Ascii, std::string(tag) + " " + theme);
            std::vector<Rec> before = ascii_records(in), after = ascii_records(out);
            std::map<std::string, Rec> a = records_of_types(before, "VISUALSTYLE"), b = records_of_types(after, "VISUALSTYLE");
            assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()), std::string(tag) + ": the same styles");
            for (const auto& [handle, rec] : a) {
                auto it = b.find(handle);
                assert_true(it != b.end(), std::string(tag) + ": style " + handle);
                std::string why = why_different(after_marker(rec, "AcDbVisualStyle"), after_marker(it->second, "AcDbVisualStyle"));
                assert_true(why.empty(), std::string(tag) + " " + theme + ": style " + handle + " written anew: " + why);
                // The XDATA (ACAD AcDbSavedByObjectVersion before 2018) as AutoCAD has it
                std::vector<Group> xa, xb;
                for (const Group& g : rec) if (g.first >= 1000) xa.push_back(g);
                for (const Group& g : it->second) if (g.first >= 1000) xb.push_back(g);
                assert_true(why_different(xa, xb).empty(), std::string(tag) + ": XDATA of " + handle);
            }
            // The round-trip XRECORDs were not touched: the typed values say what they say (no second record, no new handle)
            std::map<std::string, Rec> xa = records_of_types(before, "XRECORD"), xb = records_of_types(after, "XRECORD");
            assert_eq(static_cast<int>(xa.size()), static_cast<int>(xb.size()), std::string(tag) + ": the same XRECORDs");
            for (const auto& [handle, rec] : xa) assert_true(xb[handle] == rec, std::string(tag) + ": XRECORD " + handle + " as it was");
            // Binary: written anew and read again, the same typed values
            std::vector<uint8_t> bin_in = facit_bytes(facit_name(tag, theme, true));
            Drawing binary = load(bin_in, tag);
            for (Object& o : binary.objects()) if (o.type == ObjectType::VisualStyle) o.raw = hdxf::RawRecord();
            std::vector<uint8_t> bout = save(binary, DxfFormat::Binary, std::string(tag) + " binary");
            Drawing back = load(bout, std::string(tag) + " binary again");
            assert_true(describe_all(back, ObjectType::VisualStyle) == was, std::string(tag) + " " + theme + ": binary written anew, read again");
        }
    }
}

TEST(mvs_visual_style_changed_pairs_from_autocad) {
    // AutoCAD's pairs: a style renamed, the current style changed, a style deleted. The typed change of the first state gives
    // the second as AutoCAD saved it
    for (const char* tag : kTags) {
        std::vector<uint8_t> base = facit_bytes(facit_name(tag, "visual_styles", false));
        std::vector<uint8_t> renamed = facit_bytes(facit_name(tag, "visual_styles_renamed", false));
        std::vector<uint8_t> deleted = facit_bytes(facit_name(tag, "visual_styles_deleted", false));
        Drawing a = load(base, tag);
        Drawing r = load(renamed, tag);
        // AutoCAD's rename changes the name in ACAD_VISUALSTYLE (the key of the dictionary) and nothing in the style: its 2 is a
        // description (the reference's word) that stays HDXF_FACES
        const Object* style = style_named(a, "HDXF_FACES");
        assert_true(style != nullptr && style_named(r, "HDXF_FACES") != nullptr && style_named(r, "HDXF_FACES2") == nullptr, std::string(tag) + ": the description stays");
        assert_true(describe(style->as_visual_style()) == describe(style_named(r, "HDXF_FACES")->as_visual_style()), std::string(tag) + ": nothing in the style changed");
        Object* dict = visual_style_dictionary(a);
        assert_true(dict != nullptr, std::string(tag) + ": ACAD_VISUALSTYLE");
        for (hdxf::DictionaryEntry& e : dict->as_dictionary().entries) if (e.name == "HDXF_FACES") e.name = "HDXF_FACES2";
        std::vector<uint8_t> out = save(a, DxfFormat::Ascii, tag);
        std::vector<Rec> mine = ascii_records(out), theirs = ascii_records(renamed);
        const Rec* x = record_with_handle(mine, dict->handle);
        const Rec* y = record_with_handle(theirs, dict->handle);
        assert_true(x && y, std::string(tag) + ": the dictionary");
        std::string why = why_different(after_marker(*x, "AcDbDictionary"), after_marker(*y, "AcDbDictionary"));
        assert_true(why.empty(), std::string(tag) + ": the rename is AutoCAD's: " + why);
        // delete: the style is gone with its extension dictionary and XRECORD and nothing that pointed at it points at anything
        Drawing d = load(deleted, tag);
        assert_true(style_named(d, "HDXF_DISPLAY") == nullptr && style_named(d, "HDXF_EDGES") != nullptr, std::string(tag) + ": deleted");
        Drawing b = load(base, tag);
        const std::string gone = style_named(b, "HDXF_DISPLAY")->handle;
        auto& objects = b.objects();
        objects.erase(std::remove_if(objects.begin(), objects.end(), [&](const Object& o) { return o.handle == gone; }), objects.end());
        std::vector<uint8_t> after = save(b, DxfFormat::Ascii, std::string(tag) + " deleted");
        expect_no_new_dangling_pointers(base, after, std::string(tag) + ": a style removed");
        Drawing written = load(after, tag);
        assert_eq(count_type(written, ObjectType::VisualStyle), count_type(d, ObjectType::VisualStyle), std::string(tag) + ": the same styles as AutoCAD's");
        assert_eq(count_type(written, ObjectType::XRecord), count_type(d, ObjectType::XRecord), std::string(tag) + ": the same XRECORDs as AutoCAD's");
        assert_eq(count_type(written, ObjectType::Dictionary), count_type(d, ObjectType::Dictionary), std::string(tag) + ": the same dictionaries as AutoCAD's");
    }
}

// ---------------------------------------------------------------------------
// The XRECORD of the older forms
// ---------------------------------------------------------------------------

namespace {

// The XRECORD of a style (its extension dictionary's ACAD_XREC_ROUNDTRIP), as groups
const Object* style_xrecord(const Drawing& d, const Object& style) {
    const Object* dict = d.find_object(style.xdictionary);
    if (!dict || dict->type != ObjectType::Dictionary) return nullptr;
    for (const auto& e : dict->as_dictionary().entries) {
        if (e.name != "ACAD_XREC_ROUNDTRIP") continue;
        const Object* x = d.find_object(e.handle);
        if (x && x->type == ObjectType::XRecord) return x;
    }
    return nullptr;
}

std::string xrecord_value(const Object& x, const std::string& marker) {
    const auto& g = x.as_xrecord().data;
    for (size_t i = 0; i + 1 < g.size(); ++i) {
        if (g[i].code == 102 && g[i].value == marker) return trim(g[i + 1].value);
    }
    return "<none>";
}

} // namespace

TEST(mvs_visual_style_roundtrip_xrecord_is_synchronised_without_duplicates) {
    for (const char* tag : {"2000", "2004", "2007", "2010"}) {
        const bool form2007 = std::string(tag) != "2010";
        std::vector<uint8_t> in = facit_bytes(facit_name(tag, "visual_styles", false));
        // (a) A property after 27 changed: its XRECORD says it, the same XRECORD, the style's own record as it was
        {
            Drawing d = load(in, tag);
            Object* s = style_named(d, "HDXF_EDGES");
            const std::string handle = s->handle;
            const std::string xdict = s->xdictionary;
            const int xrecords = count_type(d, ObjectType::XRecord), dicts = count_type(d, ObjectType::Dictionary);
            const std::string xrec_handle = style_xrecord(d, *s)->handle;
            s->as_visual_style().find(VisualStyleProperty::Post2010Prop37)->value.integer = 77;
            s->as_visual_style().find(VisualStyleProperty::Post2010Prop54)->value.text = "other.tif";
            s->as_visual_style().find(VisualStyleProperty::Post2010Prop38)->value.real = 0.25;
            std::vector<uint8_t> out = save(d, DxfFormat::Ascii, tag);
            Drawing back = load(out, tag);
            const Object* r = back.find_object(handle);
            assert_true(r != nullptr, "the style");
            const Object* x = style_xrecord(back, *r);
            assert_true(x != nullptr, "its XRECORD");
            assert_eq(x->handle, xrec_handle, std::string(tag) + ": the same XRECORD");
            assert_eq(r->xdictionary, xdict, std::string(tag) + ": the same extension dictionary");
            assert_eq(xrecord_value(*x, "RTVSPost2010Prop37"), "77", std::string(tag) + ": Prop37 is " + xrecord_value(*x, "RTVSPost2010Prop37") + " (typed " + std::to_string(r->as_visual_style().find(VisualStyleProperty::Post2010Prop37)->value.integer) + ")");
            assert_eq(xrecord_value(*x, "RTVSPost2010Prop54"), "other.tif", std::string(tag) + ": Prop54");
            assert_eq(xrecord_value(*x, "RTVSPost2010Prop38"), "0.25", std::string(tag) + ": Prop38");
            assert_eq(count_type(back, ObjectType::XRecord), xrecords, std::string(tag) + ": no XRECORD added");
            assert_eq(count_type(back, ObjectType::Dictionary), dicts, std::string(tag) + ": no dictionary added");
            // The record of the style is as it was (the typed change is not in it), and so are the other styles' XRECORDs
            std::vector<Rec> before_r = ascii_records(in), after_r = ascii_records(out);
            assert_true(*record_with_handle(before_r, handle) == *record_with_handle(after_r, handle), std::string(tag) + ": the style's record unchanged");
            for (const Object& o : d.objects()) {
                if (o.type != ObjectType::XRecord || o.handle == xrec_handle) continue;
                assert_true(*record_with_handle(before_r, o.handle) == *record_with_handle(after_r, o.handle), std::string(tag) + ": XRECORD " + o.handle + " unchanged");
            }
            // And the typed values read back are the edited
            assert_eq(back.find_object(handle)->as_visual_style().find(VisualStyleProperty::Post2010Prop37)->value.integer, 77, "read back");
        }
        // (b) An operation of a property before 28: in the 2007 form it is the XRECORD's (RTVSPropertyOp4), in 2010's the record's
        {
            Drawing d = load(in, tag);
            Object* s = style_named(d, "HDXF_EDGES");
            const std::string handle = s->handle;
            s->as_visual_style().find(VisualStyleProperty::FaceOpacity)->operation = 0;
            std::vector<uint8_t> out = save(d, DxfFormat::Ascii, tag);
            Drawing back = load(out, tag);
            const Object* r = back.find_object(handle);
            assert_eq(static_cast<int>(*r->as_visual_style().find(VisualStyleProperty::FaceOpacity)->operation), 0, std::string(tag) + ": the operation read back");
            std::vector<Rec> before_r = ascii_records(in), after_r = ascii_records(out);
            bool record_same = *record_with_handle(before_r, handle) == *record_with_handle(after_r, handle);
            assert_eq(record_same ? 1 : 0, form2007 ? 1 : 0, std::string(tag) + ": 2007's operations are not in the record, 2010's are");
            if (form2007) assert_eq(xrecord_value(*style_xrecord(back, *r), "RTVSPropertyOp4"), "0", std::string(tag) + ": RTVSPropertyOp4");
        }
        // (c) The properties of the XRECORD removed from the style: the XRECORD goes with its entry and its dictionary
        {
            Drawing d = load(in, tag);
            Object* s = style_named(d, "HDXF_DISPLAY");
            const std::string handle = s->handle;
            const std::string xrec_handle = style_xrecord(d, *s)->handle;
            auto& props = s->as_visual_style().properties;
            props.erase(std::remove_if(props.begin(), props.end(), [](const VisualStyleEntry& e) { return static_cast<size_t>(e.property) >= 28; }), props.end());
            const int xrecords = count_type(d, ObjectType::XRecord);
            std::vector<uint8_t> out = save(d, DxfFormat::Ascii, tag);
            expect_no_new_dangling_pointers(in, out, std::string(tag) + ": the XRECORD removed");
            Drawing back = load(out, tag);
            assert_true(back.find_object(xrec_handle) == nullptr, std::string(tag) + ": the XRECORD is gone");
            assert_eq(count_type(back, ObjectType::XRecord), xrecords - 1, std::string(tag) + ": one XRECORD fewer");
            assert_true(back.find_object(handle)->xdictionary.empty(), std::string(tag) + ": its extension dictionary is gone");
        }
        // (d) A style that has none gets one when it has properties to keep in it: a new dictionary and XRECORD, once
        {
            Drawing d = load(in, tag);
            Object* s = style_named(d, "HDXF_DISPLAY");
            const std::string handle = s->handle;
            auto& props = s->as_visual_style().properties;
            std::vector<VisualStyleEntry> post;
            for (const VisualStyleEntry& e : props) if (static_cast<size_t>(e.property) >= 28) post.push_back(e);
            props.erase(std::remove_if(props.begin(), props.end(), [](const VisualStyleEntry& e) { return static_cast<size_t>(e.property) >= 28; }), props.end());
            save(d, DxfFormat::Ascii, std::string(tag) + " removed");
            // (its dictionary and XRECORD are gone now) put the properties back
            Object* t = style_named(d, "HDXF_DISPLAY");
            t->as_visual_style().properties.insert(t->as_visual_style().properties.end(), post.begin(), post.end());
            const int dicts = count_type(d, ObjectType::Dictionary), xrecords = count_type(d, ObjectType::XRecord);
            std::vector<uint8_t> out = save(d, DxfFormat::Ascii, tag);
            expect_no_new_dangling_pointers(in, out, std::string(tag) + ": the XRECORD made");
            Drawing back = load(out, tag);
            const Object* r = back.find_object(handle);
            assert_false(r->xdictionary.empty(), std::string(tag) + ": an extension dictionary again");
            assert_eq(count_type(back, ObjectType::Dictionary), dicts + 1, std::string(tag) + ": one dictionary");
            assert_eq(count_type(back, ObjectType::XRecord), xrecords + 1, std::string(tag) + ": one XRECORD");
            assert_eq(describe(r->as_visual_style()), describe(style_named(d, "HDXF_DISPLAY")->as_visual_style()), std::string(tag) + ": the values read back");
            // Saved again: no second record
            std::vector<uint8_t> again = save(back, DxfFormat::Ascii, tag);
            assert_true(again == out, std::string(tag) + ": written again unchanged");
        }
    }
}

TEST(mvs_visual_style_of_the_newest_form_has_no_roundtrip_record) {
    for (const char* tag : {"2013", "2018"}) {
        std::vector<uint8_t> in = facit_bytes(facit_name(tag, "visual_styles", false));
        Drawing d = load(in, tag);
        Object* s = style_named(d, "HDXF_EDGES");
        assert_true(s->xdictionary.empty() && s->as_visual_style().form == VisualStyleForm::Form2013, std::string(tag) + ": no extension dictionary");
        const int xrecords = count_type(d, ObjectType::XRecord);
        s->as_visual_style().find(VisualStyleProperty::Post2010Prop37)->value.integer = 61;
        s->as_visual_style().find(VisualStyleProperty::Post2010Prop57)->operation = 0;
        s->as_visual_style().find(VisualStyleProperty::FaceOpacity)->value.real = 0.75;
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, tag);
        Drawing back = load(out, tag);
        const VisualStyleData& r = style_named(back, "HDXF_EDGES")->as_visual_style();
        assert_eq(r.find(VisualStyleProperty::Post2010Prop37)->value.integer, 61, std::string(tag) + ": the property in the record");
        assert_eq(static_cast<int>(*r.find(VisualStyleProperty::Post2010Prop57)->operation), 0, std::string(tag) + ": its operation");
        assert_eq(r.find(VisualStyleProperty::FaceOpacity)->value.real, 0.75, std::string(tag) + ": the first property");
        assert_eq(count_type(back, ObjectType::XRecord), xrecords, std::string(tag) + ": no XRECORD made");
        assert_true(style_named(back, "HDXF_EDGES")->xdictionary.empty(), std::string(tag) + ": still no extension dictionary");
        // The record has the property in its place: 90 50 -> 90 61 at the position of the 38th value
        std::vector<Rec> recs = ascii_records(out);
        std::vector<Group> g = after_marker(*record_with_handle(recs, style_named(back, "HDXF_EDGES")->handle), "AcDbVisualStyle");
        assert_eq(trim(g[0].second), "HDXF_EDGES", "the name first");
    }
}

// ---------------------------------------------------------------------------
// A new drawing's standard objects, typed
// ---------------------------------------------------------------------------

TEST(mvs_new_drawings_have_typed_standard_materials_and_styles) {
    struct Case {
        DxfVersion version;
        VisualStyleForm form;
        size_t properties;
    };
    for (const Case& c : {Case{DxfVersion::R2007, VisualStyleForm::Form2007, 58}, Case{DxfVersion::R2010, VisualStyleForm::Form2010, 58},
                          Case{DxfVersion::R2013, VisualStyleForm::Form2013, 58}, Case{DxfVersion::R2018, VisualStyleForm::Form2013, 58}}) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            Drawing d(c.version);
            d.add_circle({0, 0, 0}, 1.0);
            std::vector<uint8_t> out = save(d, f, std::string(hdxf::acadver_string(c.version)));
            const std::string where = std::string(hdxf::acadver_string(c.version)) + (f == DxfFormat::Binary ? " binary" : " ASCII");
            // Every one typed (and none left as an untyped record)
            for (const Object& o : d.objects()) {
                assert_false(o.type == ObjectType::Unknown && (o.as_unknown().object_type == "MATERIAL" || o.as_unknown().object_type == "VISUALSTYLE"), where + ": untyped");
            }
            assert_eq(count_type(d, ObjectType::Material), 3, where + ": ByBlock, ByLayer, Global");
            assert_eq(count_type(d, ObjectType::VisualStyle), 26, where + ": 26 visual styles");
            for (const Object& o : d.objects()) {
                if (o.type != ObjectType::VisualStyle) continue;
                assert_true(o.as_visual_style().form == c.form, where + ": " + o.as_visual_style().description + " form");
                assert_eq(static_cast<int>(o.as_visual_style().properties.size()), static_cast<int>(c.properties), where + ": properties");
            }
            // Read back: the same typed values, the same count, classes once
            Drawing back = load(out, where);
            assert_true(describe_all(back, ObjectType::VisualStyle) == describe_all(d, ObjectType::VisualStyle), where + ": styles read back");
            assert_true(describe_all(back, ObjectType::Material) == describe_all(d, ObjectType::Material), where + ": materials read back");
            if (f == DxfFormat::Ascii) {
                int materials = 0, styles = 0;
                for (const Rec& rec : ascii_records(out)) {
                    if (rec.size() > 1 && rec[0].second == "CLASS" && rec[1].second == "MATERIAL") ++materials;
                    if (rec.size() > 1 && rec[0].second == "CLASS" && rec[1].second == "VISUALSTYLE") ++styles;
                }
                assert_eq(materials, 1, where + ": CLASS MATERIAL once");
                assert_eq(styles, 1, where + ": CLASS VISUALSTYLE once");
            }
            assert_true(save(back, f, where + " again") == out, where + ": written again unchanged");
        }
    }
}

TEST(mvs_set_version_converts_standard_styles_and_keeps_changed_ones) {
    Drawing d(DxfVersion::R2013);
    d.add_circle({0, 0, 0}, 1.0);
    save(d, DxfFormat::Ascii, "2013");
    auto forms = [&](VisualStyleForm f) {
        int n = 0;
        for (const Object& o : d.objects()) n += o.type == ObjectType::VisualStyle && o.as_visual_style().form == f ? 1 : 0;
        return n;
    };
    assert_eq(forms(VisualStyleForm::Form2013), 26, "26 in the newest form");
    // One changed by the program (a name of its own and a property) keeps its form where the others follow the version
    Object* mine = style_named(d, "Realistic");
    mine->as_visual_style().description = "Mine";
    mine->as_visual_style().find(VisualStyleProperty::FaceOpacity)->value.real = 0.33;
    const std::string handle = mine->handle;
    d.set_version(DxfVersion::R2010);
    std::vector<uint8_t> out2010 = save(d, DxfFormat::Ascii, "2010");
    assert_eq(forms(VisualStyleForm::Form2010), 25, "25 follow 2010");
    assert_eq(forms(VisualStyleForm::Form2013), 1, "the changed one stays");
    d.set_version(DxfVersion::R2007);
    save(d, DxfFormat::Ascii, "2007");
    assert_eq(forms(VisualStyleForm::Form2007), 25, "25 follow 2007");
    assert_true(d.find_object(handle)->as_visual_style().form == VisualStyleForm::Form2013 &&
                    d.find_object(handle)->as_visual_style().description == "Mine" &&
                    d.find_object(handle)->as_visual_style().find(VisualStyleProperty::FaceOpacity)->value.real == 0.33,
                "its form and its values are its own");
    // The round-trip records of the styles that followed: one each, and the changed style (newest form) has none
    assert_true(d.find_object(handle)->xdictionary.empty(), "the changed style has no extension dictionary");
    d.set_version(DxfVersion::R2013);
    save(d, DxfFormat::Ascii, "2013 again");
    assert_eq(forms(VisualStyleForm::Form2013), 26, "back to 26 in the newest form");
    dxf_structure::check_structure(save(d, DxfFormat::Ascii, "2013 structure"), "2013 again");
}

// ---------------------------------------------------------------------------
// Version matrix: nine versions x ASCII and binary, generated without a template
// ---------------------------------------------------------------------------

namespace {

VisualStyleData a_style_for(DxfVersion version) {
    VisualStyleData s;
    s.form = version >= DxfVersion::R2013 ? VisualStyleForm::Form2013 : version >= DxfVersion::R2010 ? VisualStyleForm::Form2010 : VisualStyleForm::Form2007;
    s.description = "MatrixStyle \xC3\xA5\xC3\xA4\xC3\xB6 " + std::string(280, 'z');
    s.style_type = 3;
    if (s.form != VisualStyleForm::Form2007) s.format_version = s.form == VisualStyleForm::Form2013 ? 3 : 2;
    s.internal_use = false;
    if (s.form == VisualStyleForm::Form2007) {
        s.edge_style_apply = 0;
        s.trailing_real = 0.0;
        s.extra_operations = {1, 1, 1, 1, 1};
    }
    for (size_t id = 0; id < hdxf::kVisualStylePropertyCount; ++id) {
        VisualStyleEntry e;
        e.property = static_cast<VisualStyleProperty>(id);
        e.value = default_visual_style_value(e.property);
        switch (e.value.kind) {
            case VisualStyleValueKind::Integer: e.value.integer = static_cast<int32_t>(id) + 1; break;
            case VisualStyleValueKind::Real:    e.value.real = 0.5 + static_cast<double>(id); break;
            case VisualStyleValueKind::Boolean: e.value.boolean = id % 2 == 0; break;
            case VisualStyleValueKind::Text:    e.value.text = "pattern.tif"; break;
            case VisualStyleValueKind::Color:
                e.value.color = hdxf::Color::from_aci(static_cast<int>(id) + 10);
                // true color only where the older forms have a group for it
                if (s.form == VisualStyleForm::Form2013 || id >= 28 || visual_style_true_color_code(e.property) != 0) e.value.color.rgb = 0x102030 + static_cast<int>(id);
                break;
        }
        e.operation = static_cast<int16_t>(id % 3 == 0 ? 0 : 1);
        // the 2007 form has no operation in the record: they are the XRECORD's; the 2010 and the newest have them inline
        s.properties.push_back(e);
    }
    return s;
}

} // namespace

TEST(mvs_material_version_matrix) {
    const ObjectCodec* codec = TypedRegistry::instance().find_object("MATERIAL");
    assert_true(codec != nullptr, "a codec");
    MaterialData want;
    want.name = "MatrixMaterial";
    want.description = "matrix";
    want.diffuse.method = 1;
    want.diffuse_map.file_name = "matrix.png";
    want.diffuse_map.transform = std::array<double, 16>{1, 0, 0, 1, 0, 1, 0, 2, 0, 0, 1, 3, 0, 0, 0, 1};
    want.opacity = 0.75;
    want.channel_flags = 63;
    auto add = [&](Drawing& d) { d.objects().push_back(make_object(ObjectType::Material, "7E01", hdxf::Boxed<MaterialData>(want))); };
    auto verify = [&](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
        const Object* o = material_named(r, "MatrixMaterial");
        if (!o) return "no material";
        if (describe(o->as_material()) != describe(want)) return "values: " + describe(o->as_material());
        return {};
    };
    hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("MATERIAL", codec->versions, add, verify);
    assert_eq(report.generated, 12, "2000 … 2018, two formats");
    assert_eq(report.named_errors, 6, "R12, R13, R14, two formats");
    assert_eq(report.autocad_verified, 14, "AutoCAD has them in R12 (none) and 2000 … 2018");
    assert_eq(report.autocad_not_savable, 4, "R13 and R14");
    assert_eq(report.autocad_not_tested, 0, "none left ej testat");
}

TEST(mvs_visual_style_version_matrix) {
    const ObjectCodec* codec = TypedRegistry::instance().find_object("VISUALSTYLE");
    assert_true(codec != nullptr, "a codec");
    auto add = [](Drawing& d) { d.objects().push_back(make_object(ObjectType::VisualStyle, "7E02", hdxf::Boxed<VisualStyleData>(a_style_for(d.version())))); };
    auto verify = [](const Drawing& r, DxfVersion v, DxfFormat) -> std::string {
        const Object* o = nullptr;
        for (const Object& x : r.objects()) if (x.type == ObjectType::VisualStyle && x.handle == "7E02") o = &x;
        if (!o) return "no style";
        // The properties after 27 of the older forms came back through the XRECORD
        VisualStyleData want = a_style_for(v);
        if (describe(o->as_visual_style()) != describe(want)) return "values differ: " + describe(o->as_visual_style());
        return {};
    };
    hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("VISUALSTYLE", codec->versions, add, verify);
    assert_eq(report.generated, 12, "2000 … 2018, two formats");
    assert_eq(report.named_errors, 6, "R12, R13, R14, two formats");
    assert_eq(report.autocad_verified, 14, "AutoCAD has them in R12 (none) and 2000 … 2018");
    assert_eq(report.autocad_not_tested, 0, "none left ej testat");
}

// ---------------------------------------------------------------------------
// Malformed and future visual styles
// ---------------------------------------------------------------------------

TEST(mvs_visual_style_malformed_records_are_kept_untyped_with_a_warning) {
    struct Case {
        const char* what;
        std::vector<Group> body;
    };
    std::vector<Group> too_many = {{2, "x"}, {70, "4"}, {177, "3"}, {291, "0"}, {70, "99"}};
    for (int i = 0; i < 99; ++i) too_many.push_back({90, "0"});
    std::vector<Group> wrong_kind = {{2, "x"}, {70, "4"}, {177, "3"}, {291, "0"}, {70, "2"}, {40, "1.0"}, {90, "0"}};     // property 0 is an integer
    const std::vector<Case> cases = {
        {"a form that is not known (177 = 7)", {{2, "x"}, {70, "4"}, {177, "7"}, {291, "0"}}},
        {"more properties (70 = 99) than there are", too_many},
        {"a value of the wrong kind for its position", wrong_kind},
        {"a count that the groups cannot hold", {{2, "x"}, {70, "4"}, {177, "3"}, {291, "0"}, {70, "5"}, {90, "1"}}},
        {"a property that is no number", {{2, "x"}, {70, "4"}, {71, "abc"}}},
        {"no name", {{70, "4"}}},
        {"no 291 in the form that has it", {{2, "x"}, {70, "4"}, {177, "2"}}},
    };
    for (const Case& c : cases) {
        Rec r = {{0, "VISUALSTYLE"}, {5, "7F01"}, {330, "0"}, {100, "AcDbVisualStyle"}};
        r.insert(r.end(), c.body.begin(), c.body.end());
        std::string text = drawing_with_objects({r});
        Drawing d = load_text(text);
        const Object* o = d.find_object("7F01");
        assert_true(o != nullptr, std::string(c.what) + ": the record");
        assert_true(o->type == ObjectType::Unknown, std::string(c.what) + ": left untyped");
        bool warned = false;
        for (const std::string& w : d.warnings()) warned = warned || w.find("VISUALSTYLE") != std::string::npos;
        assert_true(warned, std::string(c.what) + ": a warning that names the type");
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, c.what);
        assert_true(hdxf_test::lf_text(out).find(record_text(r)) != std::string::npos, std::string(c.what) + ": kept as read");
    }
    // Groups after the known ones (a later version's) stay with the record and are written back
    Rec r = {{0, "VISUALSTYLE"}, {5, "7F02"}, {330, "0"}, {100, "AcDbVisualStyle"}, {2, "x"}, {70, "4"}, {177, "3"}, {291, "0"}, {70, "1"},
             {90, "2"}, {176, "1"}, {301, "later"}};
    Drawing d = load_text(drawing_with_objects({r}));
    const Object* o = d.find_object("7F02");
    assert_true(o->type == ObjectType::VisualStyle, "typed with a trailing group");
    assert_eq(static_cast<int>(o->as_visual_style().trailing.size()), 1, "kept");
    Object* w = &*std::find_if(d.objects().begin(), d.objects().end(), [](const Object& x) { return x.handle == "7F02"; });
    w->raw = hdxf::RawRecord();
    std::vector<Rec> recs = ascii_records(save(d, DxfFormat::Ascii, "typed"));
    std::vector<Group> g = after_marker(*record_with_handle(recs, "7F02"), "AcDbVisualStyle");
    assert_eq(g.back().first, 301, "written back last");
}

TEST(mvs_visual_style_newest_form_with_missing_properties_cannot_be_written_positionally) {
    // The newest form is positional: a style without property n but with a later one has no form to be written in
    Drawing d(DxfVersion::R2018);
    VisualStyleData s = a_style_for(DxfVersion::R2018);
    s.properties.erase(s.properties.begin() + 3);
    d.objects().push_back(make_object(ObjectType::VisualStyle, "7F10", hdxf::Boxed<VisualStyleData>(s)));
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(out.empty(), "no file");
    assert_true(d.last_error().find("positional") != std::string::npos, "the error says why: " + d.last_error());
}

// ---------------------------------------------------------------------------
// Relations: what points at a material or a visual style (B19)
// ---------------------------------------------------------------------------

TEST(mvs_removed_style_is_left_out_of_every_consumer) {
    // A style that a viewport table record, a saved view and a layout viewport point at is removed from a file AutoCAD saved
    // (B19): the pointers are 0 where the record keeps its shape, left out where the entity's is optional, the style's
    // extension dictionary and XRECORD and its entry in ACAD_VISUALSTYLE go with it
    for (const char* tag : {"2007", "2010", "2013", "2018"}) {
        std::vector<uint8_t> in = facit_bytes(facit_name(tag, "visual_styles_viewport", false));
        Drawing d = load(in, tag);
        const Object* faces = style_named(d, "HDXF_FACES");
        const Object* edges = style_named(d, "HDXF_EDGES");
        assert_true(faces && edges, std::string(tag) + ": the styles");
        const std::string faces_handle = faces->handle, edges_handle = edges->handle;
        const std::string faces_xdict = faces->xdictionary;
        std::string faces_xrecord;
        if (const Object* x = style_xrecord(d, *faces)) faces_xrecord = x->handle;
        // VPORT 348 and VIEW 348 are HDXF_FACES, the layout viewport's is HDXF_EDGES (AutoCAD's own files)
        auto& objects = d.objects();
        objects.erase(std::remove_if(objects.begin(), objects.end(), [&](const Object& o) { return o.handle == faces_handle || o.handle == edges_handle; }), objects.end());
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, std::string(tag) + " styles removed");
        expect_no_new_dangling_pointers(in, out, std::string(tag) + ": a visual style removed");
        bool vport = false, view = false, viewport = false;
        for (const dxf_structure::Rec& r : dxf_structure::records_of(out)) {
            if (r.type() == "VPORT" && dxf_structure::upper_copy_of(r.value(2)) == "*ACTIVE") {
                vport = true;
                assert_true(r.value(348).empty() || r.value(348) == "0", std::string(tag) + ": the viewport table record's 348 leads nowhere: " + r.value(348));
            }
            if (r.type() == "VIEW" && r.value(2) == "HDXF_VIEW") {
                view = true;
                assert_true(r.value(348).empty() || r.value(348) == "0", std::string(tag) + ": the saved view's 348 leads nowhere: " + r.value(348));
            }
            if (r.type() == "VIEWPORT" && trim(r.value(69)) == "3") {
                viewport = true;
                assert_eq(r.value(348), "", std::string(tag) + ": the layout viewport's 348 is left out");
            }
            assert_true(r.value(5) != faces_handle && r.value(5) != edges_handle, std::string(tag) + ": a removed style is written");
            if (!faces_xrecord.empty()) assert_true(r.value(5) != faces_xrecord && r.value(5) != faces_xdict, std::string(tag) + ": its XRECORD or dictionary is written");
        }
        assert_true(vport && view && viewport, std::string(tag) + ": the consumers");
        Drawing back = load(out, tag);
        const Object* dict = visual_style_dictionary(back);
        assert_true(dict != nullptr, std::string(tag) + ": ACAD_VISUALSTYLE");
        for (const hdxf::DictionaryEntry& e : dict->as_dictionary().entries) {
            assert_true(e.name != "HDXF_FACES" && e.name != "HDXF_EDGES", std::string(tag) + ": its entry in ACAD_VISUALSTYLE is gone");
        }
    }
}

TEST(mvs_removed_material_is_left_out_of_every_consumer) {
    // A drawing made as 2007 and read again: a material a layer and an entity point at is removed (B19: the entity's 347 is
    // left out, as the pointer of an entity is; the layer's is 0 where the record keeps its shape)
    Drawing made(DxfVersion::R2007);
    hdxf::Entity& line = made.add_line({0, 0, 0}, {1, 0, 0});
    Object steel = make_object(ObjectType::Material, "7F20", hdxf::Boxed<MaterialData>(MaterialData{}));
    steel.as_material().name = "Steel";
    made.objects().push_back(steel);
    line.props.material_handle = "7F20";
    made.add_layer("Steelwork").material_handle = "7F20";
    std::vector<uint8_t> first = save(made, DxfFormat::Ascii, "first");
    Drawing d = load(first, "read");
    assert_true(material_named(d, "Steel") != nullptr, "the material is read back");
    auto& objects = d.objects();
    objects.erase(std::remove_if(objects.begin(), objects.end(), [](const Object& o) { return o.handle == "7F20"; }), objects.end());
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "after");
    dxf_structure::check_structure(out, "a material removed");
    bool line_found = false, layer_found = false;
    for (const dxf_structure::Rec& r : dxf_structure::records_of(out)) {
        if (r.type() == "LINE") {
            line_found = true;
            assert_eq(r.value(347), "", "the entity's 347 is left out (ByLayer)");
        }
        if (r.type() == "LAYER" && r.value(2) == "Steelwork") {
            layer_found = true;
            assert_true(r.value(347) != "7F20", "the layer's 347 does not point at the removed material");
        }
    }
    assert_true(line_found && layer_found, "the consumers");
}

TEST(mvs_header_pointers_to_a_removed_material_or_style_are_zero) {
    // $CMATERIAL (347), $INTERFEREOBJVS (345) and $INTERFEREVPVS (346) name the current material and the styles of the
    // interference check: the header of a file AutoCAD saved, then the records they name removed (B19: a pointer that leads
    // nowhere is 0, the others are as they were)
    for (const char* tag : {"2007", "2010", "2013", "2018"}) {
        std::vector<uint8_t> in = facit_bytes(facit_name(tag, "materials", false));
        Drawing d = load(in, tag);
        const hdxf::HeaderVar* cmaterial = d.find_header("$CMATERIAL");
        const hdxf::HeaderVar* objvs = d.find_header("$INTERFEREOBJVS");
        const hdxf::HeaderVar* vpvs = d.find_header("$INTERFEREVPVS");
        assert_true(cmaterial && objvs && vpvs && !cmaterial->values.empty() && !objvs->values.empty() && !vpvs->values.empty(), std::string(tag) + ": the variables");
        const std::string material = cmaterial->values[0].second, style = objvs->values[0].second, other_style = vpvs->values[0].second;
        assert_true(d.find_object(material) && d.find_object(style) && d.find_object(other_style), std::string(tag) + ": AutoCAD's pointers lead somewhere");
        auto& objects = d.objects();
        objects.erase(std::remove_if(objects.begin(), objects.end(), [&](const Object& o) { return o.handle == material || o.handle == style; }), objects.end());
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, tag);
        Drawing back = load(out, tag);
        assert_eq(back.find_header("$CMATERIAL")->values[0].second, "0", std::string(tag) + ": $CMATERIAL");
        assert_eq(back.find_header("$INTERFEREOBJVS")->values[0].second, "0", std::string(tag) + ": $INTERFEREOBJVS");
        assert_eq(back.find_header("$INTERFEREVPVS")->values[0].second, other_style, std::string(tag) + ": $INTERFEREVPVS (its style is still there)");
        // The same file with nothing removed has the header as it was
        Drawing same = load(in, tag);
        std::vector<uint8_t> unchanged = save(same, DxfFormat::Ascii, tag);
        assert_true(unchanged == in, std::string(tag) + ": nothing removed: written back unchanged");
    }
}

TEST(mvs_material_references_in_a_file_follow_a_purged_material_as_autocad_does) {
    // AutoCAD's own pair: the material HDXF_SIX_MAPS used by a layer and an entity, then no longer (changed to Global/ByLayer and
    // purged). Removing it here, with its users still pointing at it, gives pointers that lead nowhere: 0 in the file's records
    // that had them
    for (const char* tag : {"2007", "2010", "2013", "2018"}) {         // (a material is used by a layer or an entity from 2007)
        std::vector<uint8_t> in = facit_bytes(facit_name(tag, "materials", false));
        Drawing d = load(in, tag);
        const std::string handle = material_named(d, "HDXF_SIX_MAPS")->handle;
        int users = 0;
        for (const hdxf::Layer& l : d.layers()) users += l.material_handle == handle ? 1 : 0;
        for (const hdxf::Entity& e : d.entities()) users += e.props.material_handle == handle ? 1 : 0;
        assert_true(users >= 2, std::string(tag) + ": the layer and the circle use the material");
        auto& objects = d.objects();
        objects.erase(std::remove_if(objects.begin(), objects.end(), [&](const Object& o) { return o.handle == handle; }), objects.end());
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, tag);
        for (const Rec& rec : ascii_records(out)) {
            for (const Group& g : rec) {
                if (g.first == 347) assert_true(trim(g.second) != handle, std::string(tag) + ": a 347 still points at the removed material");
            }
        }
        expect_no_new_dangling_pointers(in, out, std::string(tag) + ": a material removed");
    }
}

// ---------------------------------------------------------------------------
// Long UTF-8 and the header of both
// ---------------------------------------------------------------------------

TEST(mvs_long_utf8_strings_in_every_version_and_format) {
    for (DxfVersion v : kVersions) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            Drawing d(v);
            MaterialData m;
            m.name = "\xE2\x82\xAC-material \xE6\x97\xA5\xE6\x9C\xAC " + std::string(400, 'y');
            m.diffuse_map.file_name = "C:/\xE6\x97\xA5\xE6\x9C\xAC/" + std::string(500, 'f') + ".png";
            d.objects().push_back(make_object(ObjectType::Material, "7F30", hdxf::Boxed<MaterialData>(m)));
            std::vector<uint8_t> out = save(d, f, std::string(hdxf::acadver_string(v)));
            Drawing back = load(out, "read back");
            const Object* o = back.find_object("7F30");
            assert_true(o && o->type == ObjectType::Material, std::string(hdxf::acadver_string(v)) + ": the material");
            assert_eq(o->as_material().name, m.name, std::string(hdxf::acadver_string(v)) + ": the name");
            assert_eq(*o->as_material().diffuse_map.file_name, *m.diffuse_map.file_name, std::string(hdxf::acadver_string(v)) + ": the file name");
        }
    }
}

TEST(mvs_object_and_entity_sizes_do_not_grow) {
    // The two types are boxed: the variant stays as large as it was (the budget of 9.0)
    if (sizeof(void*) == 8) {
        assert_true(sizeof(hdxf::Object) <= 936, "Object's size: " + std::to_string(sizeof(hdxf::Object)));
        assert_true(sizeof(hdxf::ObjectData) <= 752, "ObjectData's size: " + std::to_string(sizeof(hdxf::ObjectData)));
    }
    hdxf::Object o;
    o.data = hdxf::Boxed<hdxf::MaterialData>(MaterialData{});
    hdxf::Object copy = o;
    copy.as_material().name = "changed";
    assert_eq(o.as_material().name, "", "a copy is deep");
    o.data = hdxf::Boxed<hdxf::VisualStyleData>(VisualStyleData{});
    hdxf::Object moved = std::move(o);
    assert_true(moved.as_visual_style().properties.empty(), "moved");
}

// ---------------------------------------------------------------------------
// The corpus: every MATERIAL and VISUALSTYLE of every file there is
// ---------------------------------------------------------------------------

namespace {

std::vector<std::filesystem::path> corpus_files() {
    std::vector<std::filesystem::path> files;
    auto scan = [&](const std::filesystem::path& root) {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec)) return;
        for (auto it = std::filesystem::recursive_directory_iterator(root, ec); it != std::filesystem::recursive_directory_iterator();
             it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file()) continue;
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext == ".dxf") files.push_back(it->path());
        }
    };
    scan(hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR));
    scan(hdxf_test::source_path("doc/facit"));
    std::sort(files.begin(), files.end());
    return files;
}

bool contains_text(const std::vector<uint8_t>& bytes, const std::string& needle) {
    return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
}

bool is_binary(const std::vector<uint8_t>& bytes) {
    static const char sentinel[] = "AutoCAD Binary DXF";
    return bytes.size() >= sizeof(sentinel) - 1 && std::equal(sentinel, sentinel + sizeof(sentinel) - 1, bytes.begin());
}

} // namespace

TEST(mvs_corpus_every_material_and_visual_style_is_typed_and_written_back_as_it_was) {
    std::vector<std::filesystem::path> files = corpus_files();
    if (files.empty()) hdxf_test::skip("no corpus (build/test-data, doc/facit)");
    int checked_files = 0, materials = 0, styles = 0, written_anew = 0;
    std::vector<std::string> untyped, changed;
    for (const std::filesystem::path& path : files) {
        std::vector<uint8_t> in = hdxf_test::read_file_bytes(path);
        if (!contains_text(in, "MATERIAL") && !contains_text(in, "VISUALSTYLE")) continue;
        Drawing d;
        if (!d.load_from_memory(in.data(), in.size())) continue;       // not readable at all: the corpus tests of 9.0 say so
        ++checked_files;
        const std::string name = hdxf::utf8_from_path(path.filename());
        bool any = false;
        for (const Object& o : d.objects()) {
            if (o.type == ObjectType::Material) { ++materials; any = true; }
            else if (o.type == ObjectType::VisualStyle) { ++styles; any = true; }
            else if (o.type == ObjectType::Unknown && (o.as_unknown().object_type == "MATERIAL" || o.as_unknown().object_type == "VISUALSTYLE")) {
                untyped.push_back(name + " " + o.as_unknown().object_type + " " + o.handle);
            }
        }
        if (!any) continue;
        // Written back in its format and its version: its own materials and styles are as they were
        const DxfFormat format = is_binary(in) ? DxfFormat::Binary : DxfFormat::Ascii;
        std::vector<uint8_t> out = d.save_to_memory(format);
        if (out.empty()) continue;      // (nothing to compare)
        if (format == DxfFormat::Ascii) {
            for (const char* type : {"MATERIAL", "VISUALSTYLE"}) {
                std::map<std::string, Rec> a = records_of_types(ascii_records(in), type), b = records_of_types(ascii_records(out), type);
                for (const auto& [handle, rec] : a) {
                    auto it = b.find(handle);
                    if (it == b.end() || it->second != rec) changed.push_back(name + " " + type + " " + handle);
                }
            }
        } else {
            Drawing back;
            if (back.load_from_memory(out.data(), out.size())) {
                if (describe_all(back, ObjectType::Material) != describe_all(d, ObjectType::Material) ||
                    describe_all(back, ObjectType::VisualStyle) != describe_all(d, ObjectType::VisualStyle)) {
                    changed.push_back(name + " (binary)");
                }
            }
        }
        // And written anew from the typed values (raw emptied): the typed values read again are the same
        if (checked_files % 7 == 0) {
            Drawing original;
            original.load_from_memory(in.data(), in.size());
            for (Object& o : d.objects()) if (o.type == ObjectType::Material || o.type == ObjectType::VisualStyle) o.raw = hdxf::RawRecord();
            std::vector<uint8_t> anew = d.save_to_memory(format);
            if (!anew.empty()) {
                Drawing back;
                if (back.load_from_memory(anew.data(), anew.size())) {
                    ++written_anew;
                    if (describe_all(back, ObjectType::Material) != describe_all(original, ObjectType::Material) ||
                        describe_all(back, ObjectType::VisualStyle) != describe_all(original, ObjectType::VisualStyle)) {
                        changed.push_back(name + " (written anew)");
                    }
                }
            }
        }
    }
    std::cout << "materials and visual styles of the corpus: " << checked_files << " files, " << materials << " materials, " << styles
              << " styles typed, " << untyped.size() << " left untyped, " << written_anew << " files written anew from typed values\n";
    for (const std::string& u : untyped) std::cout << "  untyped: " << u << "\n";
    for (const std::string& c : changed) std::cout << "  changed: " << c << "\n";
    assert_true(changed.empty(), "a record that was not written back as it was: " + (changed.empty() ? std::string() : changed.front()));
}
