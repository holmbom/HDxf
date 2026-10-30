#include "test_framework.hpp"
#include "type_version_matrix.hpp"
#include "typed_corpus.hpp"
#include "hdxf.hpp"
#include "TypedRecordTypes.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Step 9.6 of phase 9 (plan 2026-10-27-fas-9-typer.md): underlays (PDFUNDERLAY/PDFDEFINITION, DWFUNDERLAY/DWFDEFINITION,
// DGNUNDERLAY/DGNDEFINITION), the coordination model (COORDINATION_MODEL, NAVISWORKSMODELDEF) and the xref panels
// (XREFPANELOBJECT, EXACXREFPANELOBJECT: no record anywhere, so they stay untyped and are kept as they are).
//
// The AutoCAD originals are doc/facit/acad_<version>_dxf_underlay_pdf*.dxf (doc/facit/fas9_underlays.scr: AutoCAD 2025's core
// opens ACadSharp's sample_AC1032 with only its PDF underlay left, and saves it in every version; then COPY, ROTATE, SCALE,
// ROTATE3D, MIRROR, ERASE and PURGE one change per file) and doc/facit/acad_<version>_dxf_xref*.dxf
// (doc/facit/fas9_coordination_xref.scr). They are local files (the facit is not in git): a test whose file is missing is
// skipped. There is no AutoCAD file with a DWF or DGN underlay, a coordination model or an xref panel (F9.3, F9.23): those
// are tested on the grammar of Autodesk's reference and reported as ej testat.
// ---------------------------------------------------------------------------

using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::Entity;
using hdxf::EntityType;
using hdxf::Object;
using hdxf::ObjectType;
using hdxf::Point2;
using hdxf::Point3;
using hdxf::UnderlayKind;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;
using namespace hdxf::internal;

namespace {

using Group = std::pair<int, std::string>;
using Rec = std::vector<Group>;

const char* const kTags[] = {"2000", "2004", "2007", "2010", "2013", "2018"};

std::filesystem::path facit_path(const std::string& name) { return hdxf_test::source_path("doc/facit/" + name); }

std::vector<uint8_t> facit_bytes(const std::string& name) {
    std::vector<uint8_t> in = hdxf_test::read_file_bytes(facit_path(name));
    if (in.empty()) hdxf_test::skip("missing doc/facit/" + name);
    return in;
}

std::string facit_name(const char* theme, const char* tag, bool binary, const std::string& state = "") {
    return std::string("acad_") + tag + "_dxf_" + theme + (state.empty() ? "" : "_" + state) + (binary ? "_bin" : "") + ".dxf";
}

std::string underlay_file(const char* tag, bool binary, const std::string& state = "") {
    return facit_name("underlay_pdf", tag, binary, state);
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

DxfFormat format_of(bool binary) { return binary ? DxfFormat::Binary : DxfFormat::Ascii; }

std::vector<const Entity*> entities_of(const Drawing& d, EntityType type) {
    std::vector<const Entity*> out;
    for (const Entity& e : d.entities()) if (e.type == type) out.push_back(&e);
    return out;
}

std::vector<const Object*> objects_of(const Drawing& d, ObjectType type) {
    std::vector<const Object*> out;
    for (const Object& o : d.objects()) if (o.type == type) out.push_back(&o);
    return out;
}

bool kept_untyped_warning(const Drawing& d) {
    return std::any_of(d.warnings().begin(), d.warnings().end(),
                       [](const std::string& w) { return w.find("kept as it was read") != std::string::npos; });
}

std::string trim(std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r')) s.pop_back();
    return s;
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

std::vector<Rec> records_named(const std::vector<Rec>& all, const std::string& name) {
    std::vector<Rec> out;
    for (const Rec& r : all) if (!r.empty() && r[0].second == name) out.push_back(r);
    return out;
}

bool is_real_code(int code) {
    return (code >= 10 && code <= 59) || (code >= 110 && code <= 149) || (code >= 210 && code <= 239);
}

// Two records say the same: the groups in the same order, text equal, doubles equal as numbers (AutoCAD cuts the 16th
// digit of a double where HDxf rounds it: the same double)
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

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps * std::max(1.0, std::fabs(a)); }

void forget_records(Drawing& d) {
    for (Entity& e : d.entities()) {
        if (e.type == EntityType::PdfUnderlay || e.type == EntityType::DwfUnderlay || e.type == EntityType::DgnUnderlay ||
            e.type == EntityType::CoordinationModel) {
            e.raw = hdxf::RawRecord();
        }
    }
    for (Object& o : d.objects()) {
        if (o.type == ObjectType::PdfDefinition || o.type == ObjectType::DwfDefinition || o.type == ObjectType::DgnDefinition ||
            o.type == ObjectType::NavisworksModelDef) {
            o.raw = hdxf::RawRecord();
        }
    }
}

std::vector<Rec> family_records(const std::vector<uint8_t>& ascii) {
    std::vector<Rec> out;
    for (const Rec& r : ascii_records(ascii)) {
        if (r.empty()) continue;
        const std::string& n = r[0].second;
        if (n == "PDFUNDERLAY" || n == "PDFDEFINITION" || n == "DWFUNDERLAY" || n == "DWFDEFINITION" || n == "DGNUNDERLAY" ||
            n == "DGNDEFINITION" || n == "COORDINATION_MODEL" || n == "NAVISWORKSMODELDEF") {
            // (a CLASS record has the name in 1, a record of the type in 0)
            out.push_back(r);
        }
    }
    return out;
}

const char* kind_name(UnderlayKind k) {
    return k == UnderlayKind::Pdf ? "PDF" : k == UnderlayKind::Dwf ? "DWF" : "DGN";
}

EntityType entity_type_of(UnderlayKind k) {
    return k == UnderlayKind::Pdf ? EntityType::PdfUnderlay : k == UnderlayKind::Dwf ? EntityType::DwfUnderlay : EntityType::DgnUnderlay;
}

ObjectType definition_type_of(UnderlayKind k) {
    return k == UnderlayKind::Pdf ? ObjectType::PdfDefinition : k == UnderlayKind::Dwf ? ObjectType::DwfDefinition : ObjectType::DgnDefinition;
}

std::string text_of(const std::vector<uint8_t>& bytes) { return std::string(bytes.begin(), bytes.end()); }

std::vector<uint8_t> bytes_of(const std::string& text) { return std::vector<uint8_t>(text.begin(), text.end()); }

} // namespace

// ---------------------------------------------------------------------------
// The registry
// ---------------------------------------------------------------------------

TEST(reference_types_the_registry_has_the_eight_codecs_and_the_aliases_and_not_the_xref_panels) {
    const TypedRegistry& registry = TypedRegistry::instance();
    for (const char* name : {"PDFUNDERLAY", "DWFUNDERLAY", "DGNUNDERLAY", "COORDINATION_MODEL"}) {
        const EntityCodec* c = registry.find_entity(name);
        assert_true(c != nullptr, std::string(name) + ": an entity codec");
        assert_eq(6, c->step, std::string(name) + ": step 9.6");
        assert_true(c->cls.cpp_name != nullptr && c->cls.application != nullptr, std::string(name) + ": a class");
        assert_true(c->write != nullptr && c->read != nullptr && c->references != nullptr, std::string(name) + ": read, write, references");
    }
    for (const char* name : {"PDFDEFINITION", "DWFDEFINITION", "DGNDEFINITION", "NAVISWORKSMODELDEF"}) {
        const ObjectCodec* c = registry.find_object(name);
        assert_true(c != nullptr, std::string(name) + ": an object codec");
        assert_eq(6, c->step, std::string(name) + ": step 9.6");
        assert_true(c->cls.cpp_name != nullptr, std::string(name) + ": a class");
    }
    assert_eq(std::string("AcDbPdfReference"), registry.find_entity("PDFUNDERLAY")->cls.cpp_name);
    assert_eq(std::string("AcDbPdfDefinition"), registry.find_object("PDFDEFINITION")->cls.cpp_name);
    assert_eq(std::string("AcBIMUnderlayDbx"), registry.find_entity("COORDINATION_MODEL")->cls.application);
    assert_eq(4095, registry.find_entity("PDFUNDERLAY")->cls.proxy_flags, "AutoCAD's flags of the reference class");
    assert_eq(1153, registry.find_object("PDFDEFINITION")->cls.proxy_flags, "AutoCAD's flags of the definition class");

    // By type, too
    assert_true(registry.find_entity(EntityType::DwfUnderlay) == registry.find_entity("DWFUNDERLAY"), "by type");
    assert_true(registry.find_object(ObjectType::DgnDefinition) == registry.find_object("DGNDEFINITION"), "by type");

    // The names a record is also read under
    assert_true(registry.find_entity("PDFREFERENCE") == registry.find_entity("PDFUNDERLAY"), "PDFREFERENCE is a PDFUNDERLAY");
    assert_true(registry.find_entity("Coordination Model") == registry.find_entity("COORDINATION_MODEL"), "the reference's name");
    assert_true(registry.find_object("AcDbNavisworksModelDef") == registry.find_object("NAVISWORKSMODELDEF"), "the reference's name");
    assert_true(registry.find_entity("PDFDEFINITION") == nullptr && registry.find_object("PDFUNDERLAY") == nullptr, "an entity is no object");
    assert_true(registry.refs_for("PDFREFERENCE") != nullptr && registry.lists_for("PDFREFERENCE") != nullptr,
                "an alias has its type's relations and lists");

    // No grammar, no codec (B116): nothing of an xref panel is in any AutoCAD file or in Autodesk's reference
    assert_false(registry.implemented("XREFPANELOBJECT"), "XREFPANELOBJECT has no codec");
    assert_false(registry.implemented("EXACXREFPANELOBJECT"), "EXACXREFPANELOBJECT has no codec");
    assert_true(registry.find_entry("XREFPANELOBJECT") && registry.find_entry("XREFPANELOBJECT")->step == 6, "still 9.6's");
}

// ---------------------------------------------------------------------------
// AutoCAD's own PDF underlay
// ---------------------------------------------------------------------------

TEST(reference_types_autocads_pdf_underlay_is_read_field_by_field_in_every_version_and_both_formats) {
    for (const char* tag : kTags) {
        std::vector<uint8_t> ascii_in = facit_bytes(underlay_file(tag, false));
        std::vector<uint8_t> binary_in = facit_bytes(underlay_file(tag, true));
        Drawing ascii = load(ascii_in, tag);
        Drawing binary = load(binary_in, std::string(tag) + " binary");
        for (const Drawing* d : {&ascii, &binary}) {
            auto underlays = entities_of(*d, EntityType::PdfUnderlay);
            assert_eq(1, static_cast<int>(underlays.size()), std::string(tag) + ": one PDF underlay");
            const hdxf::UnderlayData& u = underlays[0]->as_underlay();
            assert_eq(std::string("D07"), underlays[0]->props.handle, "AutoCAD kept ACadSharp's handle");
            assert_eq(std::string("D06"), u.definition_handle, std::string(tag) + ": the definition (340)");
            assert_true(near(16.28692563580262, u.insertion.x) && u.insertion.y == 15.0 && u.insertion.z == 0.0, "the insertion point");
            assert_true(u.scale.x == 1.0 && u.scale.y == 1.0 && u.scale.z == 1.0 && u.rotation == 0.0, "defaults are not written");
            assert_true(u.normal.x == 0.0 && u.normal.y == 0.0 && u.normal.z == 1.0, "the Z axis");
            assert_eq(3, u.flags, "clipping on and underlay on (280)");
            assert_eq(100, u.contrast, "contrast (281)");
            assert_eq(0, u.fade, "fade (282)");
            assert_true(u.clip.empty(), "no clip boundary in the sample");
            assert_eq(8, static_cast<int>(u.proxy_graphics.size()), "AutoCAD's empty proxy graphics stream: 8 bytes");
            assert_true(u.proxy_graphics[0] == 8 && u.proxy_graphics[1] == 0 && u.proxy_graphics[7] == 0, "the size 8, no commands");
            assert_eq(193, underlays[0]->props.color.aci, "the color of the sample");

            auto defs = objects_of(*d, ObjectType::PdfDefinition);
            assert_eq(1, static_cast<int>(defs.size()), std::string(tag) + ": one definition");
            assert_eq(std::string("D06"), defs[0]->handle);
            assert_eq(std::string("..\\pdf-definition.pdf"), defs[0]->as_underlay_definition().file_name, "the file (1)");
            assert_eq(std::string("1"), defs[0]->as_underlay_definition().item_name, "the page (2)");
            // reactors: the dictionary that owns it (the owner), then the underlay
            assert_eq(2, static_cast<int>(defs[0]->reactors.size()), "owner and underlay");
            assert_eq(defs[0]->owner, defs[0]->reactors[0], "the owner first");
            assert_eq(std::string("D07"), defs[0]->reactors[1], "then the entity");
            const Object* dict = d->find_object(defs[0]->owner);
            assert_true(dict && dict->type == ObjectType::Dictionary, "the owner is a dictionary");
            bool entered = false;
            for (const auto& entry : dict->as_dictionary().entries) {
                if (entry.handle == "D06") {
                    entered = true;
                    assert_eq(std::string("pdf-definition - 1"), entry.name, "AutoCAD's name for a definition: stem - page");
                }
            }
            assert_true(entered, "the definition is in its dictionary");
            assert_false(kept_untyped_warning(*d), "no record was left untyped");
        }
        // B12: nothing changed, the same bytes, in both formats
        std::vector<uint8_t> out = save(ascii, DxfFormat::Ascii, tag);
        assert_true(out == ascii_in, std::string(tag) + ": ASCII written unchanged is the file");
        std::vector<uint8_t> out_binary = save(binary, DxfFormat::Binary, tag);
        assert_true(out_binary == binary_in, std::string(tag) + ": binary written unchanged is the file");
    }
}

// A file written from records: the groups as ASCII DXF text (the records of an independent reader, edited)
namespace {
std::vector<uint8_t> rebuild(const std::vector<Rec>& records) {
    std::ostringstream s;
    for (const Rec& r : records) {
        for (const Group& g : r) {
            s << (g.first < 10 ? "  " : (g.first < 100 ? " " : "")) << g.first << "\n" << g.second << "\n";
        }
    }
    return bytes_of(s.str());
}
} // namespace

TEST(reference_types_the_typed_values_alone_write_autocads_own_records) {
    // The preserved records are emptied: each underlay and definition is written from its typed values, and has the groups,
    // the order and the values AutoCAD's own record has (doubles as the same number)
    for (const char* tag : kTags) {
        std::vector<uint8_t> in = facit_bytes(underlay_file(tag, false));
        Drawing d = load(in, tag);
        forget_records(d);
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, tag);
        std::vector<Rec> want = family_records(in), got = family_records(out);
        assert_eq(static_cast<int>(want.size()), static_cast<int>(got.size()), std::string(tag) + ": the same records");
        assert_eq(2, static_cast<int>(want.size()), "an underlay and its definition");
        for (size_t i = 0; i < want.size(); ++i) {
            std::string why = differ(want[i], got[i]);
            assert_true(why.empty(), std::string(tag) + " " + want[i][0].second + ": " + why);
        }
    }
}

TEST(reference_types_every_state_of_autocads_facit_reads_typed_and_writes_back_unchanged) {
    const char* states[] = {"copied", "rotated", "scaled", "normal", "mirrored", "instance_erased", "in_block", "all_erased", "purged"};
    const int underlays_in[] = {2, 2, 2, 2, 2, 1, 0, 0, 0};
    for (const char* tag : kTags) {
        for (size_t s = 0; s < 9; ++s) {
            for (bool binary : {false, true}) {
                std::vector<uint8_t> in = facit_bytes(underlay_file(tag, binary, states[s]));
                Drawing d = load(in, states[s]);
                assert_eq(underlays_in[s], static_cast<int>(entities_of(d, EntityType::PdfUnderlay).size()),
                          std::string(tag) + " " + states[s] + ": the underlays");
                assert_eq(1, static_cast<int>(objects_of(d, ObjectType::PdfDefinition).size()),
                          std::string(tag) + " " + states[s] + ": the definition stays (AutoCAD keeps it, also purged)");
                for (const Entity& e : d.entities()) {
                    assert_false(e.type == EntityType::Unknown, std::string(states[s]) + ": an entity was left untyped");
                    assert_false(e.raw.empty() && e.type == EntityType::PdfUnderlay, "the preserved record");
                }
                for (const Object& o : d.objects()) {
                    assert_false(o.type == ObjectType::Unknown && (o.as_unknown().object_type == "PDFDEFINITION"),
                                 std::string(states[s]) + ": the definition was left untyped");
                }
                assert_false(kept_untyped_warning(d), std::string(states[s]) + ": no warning");
                std::vector<uint8_t> out = save(d, format_of(binary), states[s]);
                assert_true(out == in, std::string(tag) + " " + states[s] + (binary ? " binary" : "") + ": written unchanged");
                if (!binary) {
                    // and from the typed values alone
                    forget_records(d);
                    std::vector<uint8_t> again = save(d, DxfFormat::Ascii, states[s]);
                    std::vector<Rec> want = family_records(in), got = family_records(again);
                    assert_eq(static_cast<int>(want.size()), static_cast<int>(got.size()), std::string(tag) + " " + states[s] + ": records");
                    for (size_t i = 0; i < want.size(); ++i) {
                        std::string why = differ(want[i], got[i]);
                        assert_true(why.empty(), std::string(tag) + " " + states[s] + " " + want[i][0].second + ": " + why);
                    }
                }
            }
        }
    }
}

TEST(reference_types_autocads_transforms_are_the_scale_rotation_normal_and_mirror_of_the_record) {
    for (const char* tag : kTags) {
        Drawing copied = load(facit_bytes(underlay_file(tag, false, "copied")), "copied");
        auto both = entities_of(copied, EntityType::PdfUnderlay);
        assert_eq(2, static_cast<int>(both.size()));
        assert_eq(both[0]->as_underlay().definition_handle, both[1]->as_underlay().definition_handle, "two instances share one definition");
        assert_true(near(116.2869256358026, both[1]->as_underlay().insertion.x) && both[1]->as_underlay().insertion.y == 65.0, "COPY by (100, 50)");
        const Object* def = copied.underlay_definition(*both[0]);
        assert_true(def != nullptr && def->handle == "D06", "the definition through the API");
        assert_eq(3, static_cast<int>(def->reactors.size()), "owner and both underlays");
        assert_eq(both[0]->props.handle, def->reactors[1], "the first underlay");
        assert_eq(both[1]->props.handle, def->reactors[2], "the second");

        Drawing rotated = load(facit_bytes(underlay_file(tag, false, "rotated")), "rotated");
        const hdxf::UnderlayData& r = entities_of(rotated, EntityType::PdfUnderlay)[0]->as_underlay();
        assert_true(near(30.0, r.rotation, 1e-12), "ROTATE 30 about the insertion point: 50");
        // (the script rotated about 16.2869,15, a base point typed with four decimals: the insertion point moved by 3e-6)
        assert_true(near(16.28692563580262, r.insertion.x, 1e-6) && near(15.0, r.insertion.y, 1e-6), "the insertion point stays, but for the base point");
        assert_true(r.scale.x == 1.0 && r.normal.z == 1.0, "nothing else");

        Drawing scaled = load(facit_bytes(underlay_file(tag, false, "scaled")), "scaled");
        const hdxf::UnderlayData& s = entities_of(scaled, EntityType::PdfUnderlay)[0]->as_underlay();
        assert_true(s.scale.x == 2.0 && s.scale.y == 2.0 && s.scale.z == 2.0, "SCALE 2: 41, 42, 43");
        assert_true(near(30.0, s.rotation, 1e-12), "the rotation stays");

        Drawing normal = load(facit_bytes(underlay_file(tag, false, "normal")), "normal");
        const hdxf::UnderlayData& n = entities_of(normal, EntityType::PdfUnderlay)[0]->as_underlay();
        assert_true(near(0.0, n.normal.x, 1e-12) && near(-1.0, n.normal.y, 1e-12) && near(0.0, n.normal.z, 1e-12),
                    "ROTATE3D 90 about X: the normal (0, -1, 0)");
        assert_true(near(-15.0, n.insertion.z), "the insertion point moved to z -15");

        Drawing mirrored = load(facit_bytes(underlay_file(tag, false, "mirrored")), "mirrored");
        auto m = entities_of(mirrored, EntityType::PdfUnderlay);
        assert_eq(2, static_cast<int>(m.size()), "the mirror replaced its source");
        const hdxf::UnderlayData& mm = m[1]->as_underlay();
        assert_true(mm.scale.x == -1.0 && mm.scale.y == 1.0 && mm.scale.z == 1.0, "a mirror in X is a negative X scale (41 -1, nothing else)");
        assert_true(near(183.7130743641974, mm.insertion.x), "mirrored about x = 150");
    }
}

TEST(reference_types_the_same_change_made_in_hdxf_is_autocads_record) {
    // Each pair of AutoCAD's facit: the earlier state with the change made in the model gives AutoCAD's later record
    for (const char* tag : kTags) {
        std::vector<uint8_t> copied_in = facit_bytes(underlay_file(tag, false, "copied"));
        auto records_after = [&](const std::string& state, const std::function<void(hdxf::UnderlayData&, const hdxf::UnderlayData&)>& change) {
            // (the numbers of the change are AutoCAD's: the transform's base point was typed with four decimals)
            std::vector<uint8_t> want_bytes = facit_bytes(underlay_file(tag, false, state));
            Drawing target = load(want_bytes, state);
            const hdxf::UnderlayData& wanted = entities_of(target, EntityType::PdfUnderlay)[0]->as_underlay();
            Drawing d = load(copied_in, "copied");
            for (Entity& e : d.entities()) if (e.type == EntityType::PdfUnderlay && e.props.handle == "D07") change(e.as_underlay(), wanted);
            std::vector<uint8_t> out = save(d, DxfFormat::Ascii, state);
            std::vector<Rec> want = family_records(want_bytes), got = family_records(out);
            assert_eq(static_cast<int>(want.size()), static_cast<int>(got.size()), std::string(tag) + " " + state + ": records");
            for (size_t i = 0; i < want.size(); ++i) {
                std::string why = differ(want[i], got[i]);
                assert_true(why.empty(), std::string(tag) + " " + state + " " + want[i][0].second + ": " + why);
            }
        };
        // ROTATE the first instance by 30 degrees: the insertion point and one group (50, after 10 20 30) are new
        records_after("rotated", [](hdxf::UnderlayData& u, const hdxf::UnderlayData& w) {
            u.insertion = w.insertion;
            u.rotation = w.rotation;
        });
        // ... and scale it by 2: 41, 42 and 43 are added (after 10 20 30, before 50)
        records_after("scaled", [](hdxf::UnderlayData& u, const hdxf::UnderlayData& w) {
            u.insertion = w.insertion;
            u.rotation = w.rotation;
            u.scale = w.scale;
        });
        // ROTATE3D: the normal (210 220 230) is added after 50 and before 280
        records_after("normal", [](hdxf::UnderlayData& u, const hdxf::UnderlayData& w) {
            u.insertion = w.insertion;
            u.rotation = w.rotation;
            u.scale = w.scale;
            u.normal = w.normal;
        });
    }
}

TEST(reference_types_erasing_an_underlay_leaves_the_shared_definition_and_takes_its_reactor) {
    for (const char* tag : kTags) {
        Drawing d = load(facit_bytes(underlay_file(tag, false, "copied")), "copied");
        auto& entities = d.entities();
        std::string erased;
        for (size_t i = 0; i < entities.size(); ++i) {
            if (entities[i].type == EntityType::PdfUnderlay && entities[i].props.handle != "D07") {
                erased = entities[i].props.handle;
                entities.erase(entities.begin() + static_cast<long>(i));
                break;
            }
        }
        assert_false(erased.empty(), "the second instance");
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, tag);
        Drawing back = load(out, tag);
        assert_eq(1, static_cast<int>(entities_of(back, EntityType::PdfUnderlay).size()), "one underlay left");
        auto defs = objects_of(back, ObjectType::PdfDefinition);
        assert_eq(1, static_cast<int>(defs.size()), "the definition stays: the other underlay uses it");
        assert_eq(2, static_cast<int>(defs[0]->reactors.size()), "owner and the remaining underlay");
        assert_true(std::find(defs[0]->reactors.begin(), defs[0]->reactors.end(), erased) == defs[0]->reactors.end(),
                    "the erased underlay is no reactor (AutoCAD keeps the handle of one it erased; a record removed from the model is gone)");
        // Erasing the last one as well: the definition is still there (AutoCAD's all_erased and purged files)
        back.entities().erase(std::remove_if(back.entities().begin(), back.entities().end(),
                                             [](const Entity& e) { return e.type == EntityType::PdfUnderlay; }),
                              back.entities().end());
        std::vector<uint8_t> out2 = save(back, DxfFormat::Ascii, tag);
        Drawing again = load(out2, tag);
        assert_eq(0, static_cast<int>(entities_of(again, EntityType::PdfUnderlay).size()));
        auto defs2 = objects_of(again, ObjectType::PdfDefinition);
        assert_eq(1, static_cast<int>(defs2.size()), "the definition is not removed with its last underlay");
        assert_eq(1, static_cast<int>(defs2[0]->reactors.size()), "only its owner is left as a reactor");
    }
}

// ---------------------------------------------------------------------------
// New underlays: every version, both formats (B101), the model's own API
// ---------------------------------------------------------------------------

namespace {

// Two underlays sharing a definition (one with every field), a third with a definition of its own
void add_three_underlays(Drawing& d, UnderlayKind kind) {
    Entity& a = d.add_underlay(kind, "C:\\plans\\plan.pdf", "1", {10, 20, 0});
    hdxf::UnderlayData& u = a.as_underlay();
    u.scale = {2.0, 3.0, 1.0};
    u.rotation = 30.0;
    u.normal = {0.0, 0.6, 0.8};
    u.flags |= hdxf::UnderlayFlag::ClippingOn | hdxf::UnderlayFlag::Monochrome;
    u.contrast = 80;
    u.fade = 25;
    u.clip = {{0.0, 0.0}, {50.5, 40.25}};
    d.add_underlay(kind, "C:\\plans\\plan.pdf", "1", {100, 0, 0});
    Entity& c = d.add_underlay(kind, "..\\sheets\\plan2.pdf", "Sheet 2", {0, 100, 0});
    c.as_underlay().clip = {{0, 0}, {10, 0}, {10, 10}, {5, 15}, {0, 10}};      // a polygon
}

std::string verify_three_underlays(const Drawing& r, UnderlayKind kind) {
    auto underlays = entities_of(r, entity_type_of(kind));
    if (underlays.size() != 3) return "underlays: " + std::to_string(underlays.size());
    auto defs = objects_of(r, definition_type_of(kind));
    if (defs.size() != 2) return "definitions: " + std::to_string(defs.size());
    const hdxf::UnderlayData& a = underlays[0]->as_underlay();
    const hdxf::UnderlayData& b = underlays[1]->as_underlay();
    const hdxf::UnderlayData& c = underlays[2]->as_underlay();
    if (a.definition_handle != b.definition_handle) return "the first two do not share their definition";
    if (a.definition_handle == c.definition_handle) return "the third shares it";
    if (a.insertion.x != 10 || a.insertion.y != 20 || a.scale.x != 2.0 || a.scale.y != 3.0 || a.scale.z != 1.0) return "placement";
    if (a.rotation != 30.0 || a.normal.y != 0.6 || a.normal.z != 0.8) return "rotation, normal";
    if (a.flags != (hdxf::UnderlayFlag::UnderlayOn | hdxf::UnderlayFlag::ClippingOn | hdxf::UnderlayFlag::Monochrome)) return "flags";
    if (a.contrast != 80 || a.fade != 25) return "contrast, fade";
    if (a.clip.size() != 2 || a.clip[1].x != 50.5 || a.clip[1].y != 40.25) return "a clip rectangle";
    if (b.scale.x != 1.0 || b.rotation != 0.0 || b.normal.z != 1.0 || !b.clip.empty() || b.flags != 2) return "defaults";
    if (c.clip.size() != 5 || c.clip[3].y != 15.0) return "a clip polygon";
    const Object* da = r.find_object(a.definition_handle);
    const Object* dc = r.find_object(c.definition_handle);
    if (!da || !dc || da->type != definition_type_of(kind) || dc->type != definition_type_of(kind)) return "the definitions";
    if (da->as_underlay_definition().file_name != "C:\\plans\\plan.pdf" || da->as_underlay_definition().item_name != "1") return "definition 1";
    if (dc->as_underlay_definition().file_name != "..\\sheets\\plan2.pdf" || dc->as_underlay_definition().item_name != "Sheet 2") return "definition 2";
    // the reactors: its owner, then the underlays that use it
    if (da->reactors.size() != 3 || da->reactors[1] != underlays[0]->props.handle || da->reactors[2] != underlays[1]->props.handle) {
        return "reactors of the shared definition: " + std::to_string(da->reactors.size());
    }
    if (dc->reactors.size() != 2) return "reactors of the other definition";
    // the dictionary of the kind, with AutoCAD's key
    const Object* dict = r.find_object(da->owner);
    if (!dict || dict->type != ObjectType::Dictionary) return "the owner is no dictionary";
    bool plan = false, sheet = false;
    for (const auto& e : dict->as_dictionary().entries) {
        plan = plan || (e.name == "plan - 1" && e.handle == da->handle);
        sheet = sheet || (e.name == "plan2 - Sheet 2" && e.handle == dc->handle);
    }
    if (!plan || !sheet) return "the dictionary's entries (stem - page)";
    return {};
}

} // namespace

TEST(reference_types_new_underlays_in_every_version_and_format) {
    const TypedRegistry& registry = TypedRegistry::instance();
    for (UnderlayKind kind : {UnderlayKind::Pdf, UnderlayKind::Dwf, UnderlayKind::Dgn}) {
        const std::string name = std::string(kind_name(kind)) + "UNDERLAY";
        const EntityCodec* codec = registry.find_entity(name);
        assert_true(codec != nullptr, name);
        auto add = [kind](Drawing& d) { add_three_underlays(d, kind); };
        auto verify = [kind](const Drawing& r, DxfVersion, DxfFormat) { return verify_three_underlays(r, kind); };
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix(name, codec->versions, add, verify);
        // 2000 … 2018 generate (12 cells); R12, R13 and R14 are the named error (6 cells)
        assert_eq(12, report.generated, name + ": generated cells");
        assert_eq(6, report.named_errors, name + ": named errors");
        if (kind == UnderlayKind::Pdf) {
            assert_eq(14, report.autocad_verified, "PDF: AutoCAD's files (R12 has none of them: AutoCAD drops them)");
        } else {
            assert_eq(0, report.autocad_verified, name + ": no AutoCAD file: ej testat");
        }
        std::cout << "  " << name << ": " << report.generated << " cells generated, " << report.named_errors << " named errors, AutoCAD verified "
                  << report.autocad_verified << ", ej testat " << report.autocad_not_tested << ", R13/R14 not savable " << report.autocad_not_savable << "\n";
    }
    // the definitions have their own matrices (the same cells)
    for (const char* name : {"PDFDEFINITION", "DWFDEFINITION", "DGNDEFINITION"}) {
        const ObjectCodec* codec = registry.find_object(name);
        assert_true(codec != nullptr, name);
        assert_true(codec->versions.can_generate(DxfVersion::R2000, DxfFormat::Ascii) && !codec->versions.can_generate(DxfVersion::R14, DxfFormat::Ascii),
                    std::string(name) + ": 2000 on");
        assert_eq(std::string("PDFDEFINITION") == name ? 14 : 0,
                  [&] { int n = 0; for (DxfVersion v : hdxf_test::all_versions) for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) n += codec->versions.at(v, f).autocad == AutoCadStatus::Verified; return n; }(),
                  std::string(name) + ": the cells AutoCAD has verified");
    }
}

TEST(reference_types_a_new_underlay_has_autocads_record_and_empty_graphics_and_adds_no_extents) {
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2018}) {
        Drawing d(v);
        Entity& e = d.add_underlay(UnderlayKind::Pdf, "..\\pdf-definition.pdf", "1", {16.28692563580262, 15.0, 0.0});
        e.props.color.aci = 193;
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "new");
        std::vector<Rec> recs = family_records(out);
        assert_eq(2, static_cast<int>(recs.size()), "the definition and the underlay");
        const Rec& def = recs[0].at(0).second == "PDFDEFINITION" ? recs[0] : recs[1];
        const Rec& und = recs[0].at(0).second == "PDFUNDERLAY" ? recs[0] : recs[1];
        // AutoCAD's own groups (the sample of ACadSharp as AutoCAD saved it): after AcDbEntity the proxy graphics 92 (160 from
        // 2010) 8 and 310 0800000000000000, then AcDbUnderlayReference 340 10 20 30 280 281 282
        std::vector<int> codes;
        for (const Group& g : und) codes.push_back(g.first);
        std::vector<int> want = {0, 5, 330, 100, 8, 62};
        if (v >= DxfVersion::R2004) want.push_back(420);
        (void)want;
        bool saw_graphics = false, saw_subclass = false;
        for (size_t i = 0; i < und.size(); ++i) {
            if (und[i].first == (v >= DxfVersion::R2010 ? 160 : 92)) {
                saw_graphics = true;
                assert_eq(std::string("8"), trim(und[i].second), "the stream's length");
                assert_true(und[i + 1].first == 310 && trim(und[i + 1].second) == "0800000000000000", "AutoCAD's 8 bytes");
            }
            if (und[i].first == 100 && trim(und[i].second) == "AcDbUnderlayReference") {
                saw_subclass = true;
                assert_eq(340, und[i + 1].first);
                assert_eq(10, und[i + 2].first);
                assert_eq(280, und[i + 5].first, "no 41/42/43, no 50, no 210: the defaults are not written");
                assert_eq(std::string("2"), trim(und[i + 5].second), "the underlay is on");
                assert_eq(281, und[i + 6].first);
                assert_eq(282, und[i + 7].first);
                assert_eq(static_cast<int>(i + 8), static_cast<int>(und.size()), "nothing after 282 without a clip");
            }
        }
        assert_true(saw_graphics && saw_subclass, "graphics and the subclass");
        assert_eq(std::string("PDFDEFINITION"), def[0].second);
        // the definition: 330 owner reactors then the groups
        assert_eq(std::string("..\\pdf-definition.pdf"), trim(def[def.size() - 2].second), "the file");
        assert_eq(std::string("1"), trim(def[def.size() - 1].second), "the page");

        // HDxf has no PDF reader: the underlay adds nothing to the extents
        hdxf::Point3 lo, hi;
        assert_false(d.entity_extents(d.entities()[0], lo, hi), "an underlay has no extents of its own");
    }
}

TEST(reference_types_removed_targets_and_ownership_b19) {
    for (bool read_back : {false, true}) {
        Drawing d(DxfVersion::R2018);
        add_three_underlays(d, UnderlayKind::Pdf);
        if (read_back) {
            std::vector<uint8_t> bytes = save(d, DxfFormat::Ascii, "first");
            d = load(bytes, "read back");
        }
        // The definition of the first two is removed (with its dictionary entry)
        std::string gone = entities_of(d, EntityType::PdfUnderlay)[0]->as_underlay().definition_handle;
        for (Object& o : d.objects()) {
            if (o.type != ObjectType::Dictionary) continue;
            auto& entries = o.as_dictionary().entries;
            entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const hdxf::DictionaryEntry& e) { return e.handle == gone; }), entries.end());
        }
        d.objects().erase(std::remove_if(d.objects().begin(), d.objects().end(), [&](const Object& o) { return o.handle == gone; }), d.objects().end());
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "removed");
        Drawing back = load(out, "removed");
        auto underlays = entities_of(back, EntityType::PdfUnderlay);
        assert_eq(3, static_cast<int>(underlays.size()), "the underlays stay");
        assert_eq(std::string("0"), underlays[0]->as_underlay().definition_handle, "340 is none (0): nothing dangles");
        assert_eq(std::string("0"), underlays[1]->as_underlay().definition_handle, "340 is none (0)");
        assert_true(underlays[2]->as_underlay().definition_handle != "0", "the third keeps its own definition");
        assert_eq(1, static_cast<int>(objects_of(back, ObjectType::PdfDefinition).size()), "the removed definition is gone");
        // the placement of an underlay with no definition is kept
        assert_eq(30.0, underlays[0]->as_underlay().rotation, "the rest of the record is kept");
        assert_eq(2, static_cast<int>(underlays[0]->as_underlay().clip.size()), "the clip too");
        assert_true(std::any_of(d.warnings().begin(), d.warnings().end(), [](const std::string& w) { return w.find("no longer exists") != std::string::npos; }),
                    "a warning says the definition is gone");
    }
}

TEST(reference_types_long_utf8_names_and_characters_above_ffff_in_every_version) {
    // A file name and a sheet name of Swedish letters, a character above U+FFFF (two surrogate escapes in the file) and a
    // long path
    std::string file = "C:\\Ritningar \xC3\xA5\xC3\xA4\xC3\xB6\\F\xC3\xB6rgrening \xF0\x9D\x84\x9E";
    for (int i = 0; i < 20; ++i) file += "\\niv\xC3\xA5" + std::to_string(i);
    file += "\\kabel.pdf";
    const std::string item = "Blad \xC3\xB6 1";
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018}) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            Drawing d(v);
            d.add_underlay(UnderlayKind::Pdf, file, item, {1, 2, 3});
            std::vector<uint8_t> out = save(d, f, "utf8");
            Drawing back = load(out, "utf8");
            auto defs = objects_of(back, ObjectType::PdfDefinition);
            assert_eq(1, static_cast<int>(defs.size()));
            assert_eq(file, defs[0]->as_underlay_definition().file_name, std::string("the file name, ") + hdxf_test::version_label(v));
            assert_eq(item, defs[0]->as_underlay_definition().item_name, "the item name");
            std::vector<uint8_t> again = save(back, f, "utf8 again");
            assert_true(again == out, "written again unchanged");
        }
    }
}

// ---------------------------------------------------------------------------
// Records that do not fit: kept as read, with a warning (B105)
// ---------------------------------------------------------------------------

namespace {

std::vector<Rec> all_records_of_2018() { return ascii_records(facit_bytes(underlay_file("2018", false))); }

Rec* find_record(std::vector<Rec>& all, const std::string& name) {
    for (Rec& r : all) if (!r.empty() && r[0].second == name) return &r;
    return nullptr;
}

} // namespace

TEST(reference_types_records_that_do_not_fit_stay_untyped_and_preserved_with_a_warning) {
    // (a) no AcDbUnderlayReference subclass
    {
        std::vector<Rec> all = all_records_of_2018();
        Rec* r = find_record(all, "PDFUNDERLAY");
        assert_true(r != nullptr, "the record");
        for (Group& g : *r) if (g.first == 100 && trim(g.second) == "AcDbUnderlayReference") g.second = "AcDbSomethingElse";
        std::vector<uint8_t> bytes = rebuild(all);
        Drawing d = load(bytes, "no subclass");
        assert_eq(0, static_cast<int>(entities_of(d, EntityType::PdfUnderlay).size()), "not typed");
        bool warned = false;
        for (const std::string& w : d.warnings()) warned = warned || (w.find("PDFUNDERLAY") != std::string::npos && w.find("kept as it was read") != std::string::npos);
        assert_true(warned, "a warning names the record");
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "untyped");
        assert_true(out == bytes, "written back as it was read");
    }
    // (b) a flag that is no number
    {
        std::vector<Rec> all = all_records_of_2018();
        Rec* r = find_record(all, "PDFUNDERLAY");
        for (Group& g : *r) if (g.first == 280) g.second = "abc";
        Drawing d = load(rebuild(all), "bad flags");
        assert_eq(0, static_cast<int>(entities_of(d, EntityType::PdfUnderlay).size()), "not typed");
        assert_true(kept_untyped_warning(d), "a warning");
    }
    // (c) a flag that does not fit an int16
    {
        std::vector<Rec> all = all_records_of_2018();
        Rec* r = find_record(all, "PDFUNDERLAY");
        for (Group& g : *r) if (g.first == 280) g.second = "70000";
        Drawing d = load(rebuild(all), "big flags");
        assert_eq(0, static_cast<int>(entities_of(d, EntityType::PdfUnderlay).size()), "not typed");
    }
    // (d) a definition without its page
    {
        std::vector<Rec> all = all_records_of_2018();
        Rec* r = find_record(all, "PDFDEFINITION");
        r->pop_back();
        Drawing d = load(rebuild(all), "no page");
        assert_eq(0, static_cast<int>(objects_of(d, ObjectType::PdfDefinition).size()), "not typed");
        assert_true(kept_untyped_warning(d), "a warning");
    }
    // (e) a group after 282: a later version of the record. Kept in the preserved record, typed, with a warning; nothing is lost
    {
        std::vector<Rec> all = all_records_of_2018();
        Rec* r = find_record(all, "PDFUNDERLAY");
        r->push_back({289, "5"});
        std::vector<uint8_t> bytes = rebuild(all);
        Drawing d = load(bytes, "future group");
        assert_eq(1, static_cast<int>(entities_of(d, EntityType::PdfUnderlay).size()), "typed");
        assert_true(std::any_of(d.warnings().begin(), d.warnings().end(), [](const std::string& w) { return w.find("group(s) after the ones HDxf knows") != std::string::npos; }),
                    "a warning for the group");
        entities_of(d, EntityType::PdfUnderlay);
        for (Entity& e : d.entities()) if (e.type == EntityType::PdfUnderlay) e.as_underlay().fade = 10;
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "future");
        std::vector<Rec> after = ascii_records(out);
        Rec* w = find_record(after, "PDFUNDERLAY");
        bool kept = false, changed = false;
        for (const Group& g : *w) {
            kept = kept || (g.first == 289 && trim(g.second) == "5");
            changed = changed || (g.first == 282 && trim(g.second) == "10");
        }
        assert_true(kept && changed, "the group stays and the fade changed");
    }
}

// ---------------------------------------------------------------------------
// PDFREFERENCE: a name ODA File Converter gives the entity
// ---------------------------------------------------------------------------

TEST(reference_types_pdfreference_is_read_as_a_pdf_underlay_and_keeps_its_name) {
    std::vector<Rec> all = all_records_of_2018();
    Rec* r = find_record(all, "PDFUNDERLAY");
    (*r)[0].second = "PDFREFERENCE";
    std::vector<uint8_t> bytes = rebuild(all);
    Drawing d = load(bytes, "PDFREFERENCE");
    auto underlays = entities_of(d, EntityType::PdfUnderlay);
    assert_eq(1, static_cast<int>(underlays.size()), "typed as a PDF underlay");
    assert_eq(std::string("D06"), underlays[0]->as_underlay().definition_handle);
    assert_false(kept_untyped_warning(d), "read");
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "alias");
    assert_true(out == bytes, "written unchanged: its own name is kept");
    // changed: still under its own name, with the changed group
    for (Entity& e : d.entities()) if (e.type == EntityType::PdfUnderlay) e.as_underlay().rotation = 45.0;
    std::vector<uint8_t> changed = save(d, DxfFormat::Ascii, "alias changed");
    std::vector<Rec> after = ascii_records(changed);
    Rec* w = find_record(after, "PDFREFERENCE");
    assert_true(w != nullptr, "the record keeps the name PDFREFERENCE");
    bool rotation = false;
    for (const Group& g : *w) rotation = rotation || (g.first == 50 && std::atof(g.second.c_str()) == 45.0);
    assert_true(rotation, "and has the rotation");
    // a new underlay is written under AutoCAD's name
    Drawing fresh(DxfVersion::R2018);
    fresh.add_underlay(UnderlayKind::Pdf, "a.pdf", "1", {0, 0, 0});
    std::vector<Rec> fresh_records = ascii_records(save(fresh, DxfFormat::Ascii, "fresh"));
    assert_true(find_record(fresh_records, "PDFUNDERLAY") != nullptr && find_record(fresh_records, "PDFREFERENCE") == nullptr, "PDFUNDERLAY");
}

// ---------------------------------------------------------------------------
// The coordination model (no AutoCAD file has one: the grammar is the reference's and phase 6's, ej testat, F9.23)
// ---------------------------------------------------------------------------

namespace {

const std::array<double, 16> kModelMatrix = {0, -1, 0, 10, 1, 0, 0, 20, 0, 0, 1, 30, 0, 0, 0, 1};

void add_model(Drawing& d) {
    Entity& m = d.add_coordination_model("C:\\models\\plant.nwd", {0, 0, 0}, {2, 3, 4}, kModelMatrix, 25.4);
    m.as_coordination_model().version = 1;
    d.add_coordination_model("D:\\bim\\site \xC3\xA5.nwc", {-5, -5, 0}, {50, 40, 12});
}

std::string verify_models(const Drawing& r) {
    auto models = entities_of(r, EntityType::CoordinationModel);
    if (models.size() != 2) return "models: " + std::to_string(models.size());
    auto defs = objects_of(r, ObjectType::NavisworksModelDef);
    if (defs.size() != 2) return "definitions: " + std::to_string(defs.size());
    const hdxf::CoordinationModelData& a = models[0]->as_coordination_model();
    if (a.matrix != kModelMatrix || a.unit_factor != 25.4) return "the transform (16 values) and the unit factor (the 17th 40)";
    const Object* def = r.find_object(a.definition_handle);
    if (!def || def->type != ObjectType::NavisworksModelDef) return "the definition";
    const hdxf::NavisworksModelDefData& d = def->as_navisworks_model_def();
    if (d.path != "C:\\models\\plant.nwd" || d.min.x != 0 || d.max.z != 4 || d.load || !d.host_visible) return "the definition's values (the two 290 apart)";
    const hdxf::NavisworksModelDefData& e = r.find_object(models[1]->as_coordination_model().definition_handle)->as_navisworks_model_def();
    if (e.path != "D:\\bim\\site \xC3\xA5.nwc" || e.min.x != -5) return "the second definition";
    return {};
}

} // namespace

TEST(reference_types_coordination_model_new_in_every_version_and_format) {
    const TypedRegistry& registry = TypedRegistry::instance();
    const EntityCodec* model = registry.find_entity("COORDINATION_MODEL");
    const ObjectCodec* definition = registry.find_object("NAVISWORKSMODELDEF");
    assert_true(model && definition, "both codecs");
    // (a drawing has both: the entity's matrix is run, and the definition's cells are the same ones)
    {
        auto verify = [](const Drawing& r, DxfVersion, DxfFormat) { return verify_models(r); };
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("COORDINATION_MODEL", model->versions, add_model, verify);
        // 2013 and 2018 generate (4 cells); R12 … 2010 are the named error (14 cells): nothing is claimed of AutoCAD (ej testat)
        assert_eq(4, report.generated, "generated cells");
        assert_eq(14, report.named_errors, "named errors");
        assert_eq(0, report.autocad_verified, "no AutoCAD file: ej testat");
        std::cout << "  COORDINATION_MODEL and NAVISWORKSMODELDEF: " << report.generated << " cells generated, " << report.named_errors
                  << " named errors, ej testat " << report.autocad_not_tested << ", R13/R14 not savable " << report.autocad_not_savable << "\n";
    }
    for (DxfVersion v : hdxf_test::all_versions) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const VersionCell& a = model->versions.at(v, f);
            const VersionCell& b = definition->versions.at(v, f);
            assert_true(a.generation == b.generation && a.basis == b.basis && a.autocad == b.autocad,
                        std::string("the definition has the model's cell: ") + hdxf_test::version_label(v));
        }
    }
    // The old messages are kept: before 2013 "requires DXF 2013 or later"
    for (DxfVersion v : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2010}) {
        Drawing d(v);
        add_model(d);
        assert_true(d.save_to_memory(DxfFormat::Ascii).empty(), "refused");
        assert_true(d.last_error().find("requires DXF 2013 or later") != std::string::npos, d.last_error());
    }
}

TEST(reference_types_coordination_model_typed_values_alone_write_the_same_records) {
    for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
        Drawing d(DxfVersion::R2018);
        add_model(d);
        std::vector<uint8_t> first = save(d, format, "first");
        Drawing back = load(first, "back");
        assert_eq(std::string(), verify_models(back), "read back");
        std::vector<uint8_t> unchanged = save(back, format, "unchanged");
        assert_true(unchanged == first, "written unchanged");
        forget_records(back);
        std::vector<uint8_t> anew = save(back, format, "from the typed values");
        // (the file's $VERSIONGUID is new when a record is written anew: the records are what is compared)
        Drawing again = load(anew, "from the typed values");
        assert_eq(std::string(), verify_models(again), "the typed values alone: read back");
        if (format == DxfFormat::Ascii) {
            std::vector<Rec> want = family_records(first), got = family_records(anew);
            assert_eq(static_cast<int>(want.size()), static_cast<int>(got.size()), "records");
            for (size_t i = 0; i < want.size(); ++i) {
                std::string why = differ(want[i], got[i]);
                assert_true(why.empty(), want[i][0].second + ": " + why);
            }
        }
    }
}

TEST(reference_types_coordination_model_groups_are_the_grammars_and_a_short_one_stays_untyped) {
    Drawing d(DxfVersion::R2018);
    add_model(d);
    std::vector<uint8_t> bytes = save(d, DxfFormat::Ascii, "model");
    std::vector<Rec> all = ascii_records(bytes);
    Rec* model = find_record(all, "COORDINATION_MODEL");
    assert_true(model != nullptr, "the entity");
    std::vector<int> codes;
    bool in = false;
    for (const Group& g : *model) {
        if (g.first == 100) in = trim(g.second) == "AcDbNavisworksModel";
        else if (in) codes.push_back(g.first);
    }
    std::vector<int> want = {70, 340};
    want.insert(want.end(), 17, 40);
    assert_true(codes == want, "70, 340 and 17 x 40");
    Rec* def = find_record(all, "NAVISWORKSMODELDEF");
    codes.clear();
    in = false;
    for (const Group& g : *def) {
        if (g.first == 100) in = trim(g.second) == "AcDbNavisworksModelDef";
        else if (in) codes.push_back(g.first);
    }
    assert_true(codes == std::vector<int>({70, 1, 290, 10, 20, 30, 11, 21, 31, 290}), "70, the file, load, minimum, maximum, host visibility");

    // The 17th value taken away: the transform is whole, the unit factor is the default (the reference has 16 values)
    {
        std::vector<Rec> edited = all;
        Rec* m = find_record(edited, "COORDINATION_MODEL");
        for (size_t i = m->size(); i-- > 0;) {
            if ((*m)[i].first == 40) { m->erase(m->begin() + static_cast<long>(i)); break; }
        }
        Drawing r = load(rebuild(edited), "16 values");
        auto models = entities_of(r, EntityType::CoordinationModel);
        assert_eq(2, static_cast<int>(models.size()), "typed");
        assert_eq(1.0, models[0]->as_coordination_model().unit_factor, "no unit factor: 1");
    }
    // Fewer than 16: not a transform. The record stays as it was read, with a warning that names it
    {
        std::vector<Rec> edited = all;
        Rec* m = find_record(edited, "COORDINATION_MODEL");
        for (int removed = 0; removed < 3; ++removed) {
            for (size_t i = m->size(); i-- > 0;) {
                if ((*m)[i].first == 40) { m->erase(m->begin() + static_cast<long>(i)); break; }
            }
        }
        std::vector<uint8_t> bytes2 = rebuild(edited);
        Drawing r = load(bytes2, "13 values");
        assert_eq(1, static_cast<int>(entities_of(r, EntityType::CoordinationModel).size()), "the first one is not typed");
        bool warned = false;
        for (const std::string& w : r.warnings()) warned = warned || (w.find("COORDINATION_MODEL") != std::string::npos && w.find("kept as it was read") != std::string::npos);
        assert_true(warned, "a warning names the record");
        std::vector<uint8_t> out = save(r, DxfFormat::Ascii, "kept");
        assert_true(out == bytes2, "written as it was read");
    }
    // Not a number in a transform value
    {
        std::vector<Rec> edited = all;
        Rec* m = find_record(edited, "COORDINATION_MODEL");
        for (Group& g : *m) if (g.first == 40) { g.second = "x"; break; }
        Drawing r = load(rebuild(edited), "not a number");
        assert_eq(1, static_cast<int>(entities_of(r, EntityType::CoordinationModel).size()), "the first one is not typed");
    }
}

TEST(reference_types_coordination_names_of_the_reference_are_read_and_the_models_graphics_are_kept) {
    Drawing d(DxfVersion::R2018);
    add_model(d);
    std::vector<uint8_t> bytes = save(d, DxfFormat::Ascii, "model");
    std::vector<Rec> all = ascii_records(bytes);
    // The reference's names: "Coordination Model" and AcDbNavisworksModelDef
    {
        std::vector<Rec> edited = all;
        for (Rec& r : edited) {
            if (r[0].second == "COORDINATION_MODEL") r[0].second = "Coordination Model";
            if (r[0].second == "NAVISWORKSMODELDEF") r[0].second = "AcDbNavisworksModelDef";
        }
        std::vector<uint8_t> named = rebuild(edited);
        Drawing r = load(named, "names");
        assert_eq(std::string(), verify_models(r), "typed under the reference's names");
        std::vector<uint8_t> out = save(r, DxfFormat::Ascii, "names");
        assert_true(out == named, "written back under the names it had");
    }
    // Its proxy graphics (160 + 310 of AcDbEntity): bytes, kept, written back, and only when $PROXYGRAPHICS is on
    {
        std::vector<Rec> edited = all;
        for (Rec& r : edited) {
            if (r[0].second != "COORDINATION_MODEL") continue;
            for (size_t i = 0; i < r.size(); ++i) {
                if (r[i].first == 100 && trim(r[i].second) == "AcDbEntity") {
                    size_t at = i + 1;
                    while (at < r.size() && r[at].first != 100) ++at;
                    r.insert(r.begin() + static_cast<long>(at), {{160, "12"}, {310, "0C0000000000000001020304"}});
                    break;
                }
            }
            break;
        }
        std::vector<uint8_t> with = rebuild(edited);
        Drawing r = load(with, "graphics");
        auto models = entities_of(r, EntityType::CoordinationModel);
        assert_eq(12, static_cast<int>(models[0]->as_coordination_model().proxy_graphics.size()), "the 12 bytes");
        assert_true(models[0]->as_coordination_model().proxy_graphics[8] == 1, "as written");
        std::vector<uint8_t> out = save(r, DxfFormat::Ascii, "graphics");
        assert_true(out == with, "written unchanged");
        forget_records(r);
        std::vector<uint8_t> anew = save(r, DxfFormat::Ascii, "graphics from typed values");
        bool has = false;
        for (const Rec& rec : ascii_records(anew)) {
            if (rec[0].second != "COORDINATION_MODEL") continue;
            for (const Group& g : rec) has = has || (g.first == 310 && trim(g.second) == "0C0000000000000001020304");
        }
        assert_true(has, "the typed bytes are written again");
        r.set_header_int("$PROXYGRAPHICS", 70, 0);
        std::vector<uint8_t> none = save(r, DxfFormat::Ascii, "no graphics");
        for (const Rec& rec : ascii_records(none)) {
            if (rec[0].second != "COORDINATION_MODEL") continue;
            for (const Group& g : rec) assert_false(g.first == 310, "$PROXYGRAPHICS 0: no graphics");
        }
    }
}

TEST(reference_types_the_removed_definition_of_a_model_is_no_pointer_and_a_removed_model_leaves_its_definition) {
    Drawing d(DxfVersion::R2018);
    add_model(d);
    std::vector<uint8_t> bytes = save(d, DxfFormat::Ascii, "model");
    Drawing r = load(bytes, "model");
    std::string gone = entities_of(r, EntityType::CoordinationModel)[1]->as_coordination_model().definition_handle;
    for (Object& o : r.objects()) {
        if (o.type != ObjectType::Dictionary) continue;
        auto& entries = o.as_dictionary().entries;
        entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const hdxf::DictionaryEntry& e) { return e.handle == gone; }), entries.end());
    }
    r.objects().erase(std::remove_if(r.objects().begin(), r.objects().end(), [&](const Object& o) { return o.handle == gone; }), r.objects().end());
    std::vector<uint8_t> out = save(r, DxfFormat::Ascii, "removed");
    Drawing back = load(out, "removed");
    auto models = entities_of(back, EntityType::CoordinationModel);
    assert_eq(2, static_cast<int>(models.size()));
    assert_eq(std::string("0"), models[1]->as_coordination_model().definition_handle, "340 is none");
    assert_true(models[0]->as_coordination_model().definition_handle != "0", "the other keeps its definition");
    // a model removed: its definition stays (it is the dictionary's)
    back.entities().erase(back.entities().begin());
    std::vector<uint8_t> out2 = save(back, DxfFormat::Ascii, "model removed");
    Drawing again = load(out2, "model removed");
    assert_eq(1, static_cast<int>(entities_of(again, EntityType::CoordinationModel).size()));
    assert_eq(1, static_cast<int>(objects_of(again, ObjectType::NavisworksModelDef).size()), "its definition stays");
}

// ---------------------------------------------------------------------------
// Xrefs and the xref panels
// ---------------------------------------------------------------------------

TEST(reference_types_autocads_xref_files_read_and_write_back_unchanged_and_have_no_xref_panel) {
    const char* states[] = {"", "rotated", "mirrored", "unloaded", "path_changed", "detached"};
    for (const char* tag : {"r12", "2000", "2004", "2007", "2010", "2013", "2018"}) {
        for (const char* state : states) {
            for (bool binary : {false, true}) {
                std::vector<uint8_t> in = facit_bytes(facit_name("xref", tag, binary, state));
                Drawing d = load(in, state);
                std::vector<uint8_t> out = save(d, format_of(binary), state);
                assert_true(out == in, std::string(tag) + " xref " + state + (binary ? " binary" : "") + ": written unchanged");
                assert_false(kept_untyped_warning(d), "no warning");
                // The command -XREF makes no panel object: no record of either type is in a file AutoCAD saved
                for (const Object& o : d.objects()) {
                    assert_false(o.type == ObjectType::Unknown && (o.as_unknown().object_type == "XREFPANELOBJECT" || o.as_unknown().object_type == "EXACXREFPANELOBJECT"),
                                 "a panel object in an AutoCAD file");
                }
                // (R12 has the names in capitals)
                const std::string block_name = std::string(tag) == "r12" ? "FAS9_XREF_SOURCE" : "fas9_xref_source";
                if (std::string(state) == "detached") {
                    assert_true(d.find_block(block_name) == nullptr, "the block is gone with the xref");
                    continue;
                }
                const hdxf::Block* block = d.find_block(block_name);
                assert_true(block != nullptr, std::string(tag) + " " + state + (binary ? " binary" : "") + ": the xref's block");
                assert_true(block->xref_path == "fas9_xref_source.dwg" || block->xref_path == ".\\fas9_xref_source.dwg", "the xref's path: " + block->xref_path);
                if (std::string(state) == "path_changed") assert_eq(std::string(".\\fas9_xref_source.dwg"), block->xref_path, "the changed path");
                assert_eq(std::string(state) == "unloaded" ? 4 : 36, block->flags, std::string(state) + ": the block's flags (4 xref, 32 resolved)");
                auto inserts = std::vector<const Entity*>();
                for (const Entity& e : d.entities()) if (e.type == EntityType::Insert) inserts.push_back(&e);
                assert_eq(1, static_cast<int>(inserts.size()), "the INSERT of the xref");
                const hdxf::InsertData& ins = inserts[0]->as_insert();
                assert_eq(block_name, ins.block_name);
                if (std::string(state) == "rotated") assert_true(near(30.0, ins.rotation, 1e-9), "ROTATE 30");
                if (std::string(state) == "mirrored" || std::string(state) == "unloaded" || std::string(state) == "path_changed") {
                    assert_eq(-1.0, ins.x_scale, "a mirror is a negative X scale");
                    assert_true(near(150.0, ins.rotation, 1e-9), "and the rotation 150");
                }
            }
        }
    }
}

TEST(reference_types_xref_panel_objects_stay_as_they_are_no_grammar_no_codec) {
    // Neither XREFPANELOBJECT nor EXACXREFPANELOBJECT has a record in any AutoCAD file or a page in Autodesk's reference, so there is
    // nothing to type (B116: no invented codec): a record of either is an unknown object, preserved byte for byte, with no warning
    Drawing d(DxfVersion::R2018);
    d.add_line({0, 0, 0}, {1, 1, 0});
    std::vector<uint8_t> bytes = save(d, DxfFormat::Ascii, "base");
    std::vector<Rec> all = ascii_records(bytes);
    for (const char* name : {"XREFPANELOBJECT", "EXACXREFPANELOBJECT"}) {
        std::vector<Rec> edited = all;
        Rec panel = {{0, name}, {5, "7C01"}, {102, "{ACAD_REACTORS"}, {330, "C"}, {102, "}"}, {330, "C"}, {100, "AcDbXrefPanelSomething"}, {90, "3"}, {1, "C:\\x\\a.dwg"}, {340, "7C02"}};
        for (size_t i = 0; i < edited.size(); ++i) {
            if (edited[i][0].second == "DICTIONARY") { edited.insert(edited.begin() + static_cast<long>(i), panel); break; }
        }
        std::vector<uint8_t> text = rebuild(edited);
        Drawing r = load(text, name);
        const Object* o = r.find_object("7C01");
        assert_true(o != nullptr && o->type == ObjectType::Unknown && o->as_unknown().object_type == name, std::string(name) + ": an unknown object");
        assert_false(kept_untyped_warning(r), "no warning: nothing was tried");
        std::vector<uint8_t> out = save(r, DxfFormat::Ascii, name);
        assert_true(out == text, std::string(name) + ": written back byte for byte");
    }
    assert_false(TypedRegistry::instance().implemented("XREFPANELOBJECT") || TypedRegistry::instance().implemented("EXACXREFPANELOBJECT"), "no codec");
}

// ---------------------------------------------------------------------------
// CLASSES
// ---------------------------------------------------------------------------

TEST(reference_types_the_classes_of_a_new_drawing_are_autocads_entries_with_the_instance_counts) {
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2010, DxfVersion::R2018}) {
        Drawing d(v);
        d.add_underlay(UnderlayKind::Pdf, "a.pdf", "1", {0, 0, 0});
        d.add_underlay(UnderlayKind::Pdf, "a.pdf", "1", {5, 0, 0});
        d.add_underlay(UnderlayKind::Dgn, "m.dgn", "Default", {9, 0, 0});
        std::vector<Rec> all = ascii_records(save(d, DxfFormat::Ascii, "classes"));
        auto class_of = [&](const std::string& name) -> const Rec* {
            for (const Rec& r : all) {
                if (r.size() > 1 && r[0].second == "CLASS" && trim(r[1].second) == name) return &r;
            }
            return nullptr;
        };
        struct Want { const char* name; const char* cpp; int flags; int entity; int count; };
        const Want wants[] = {{"PDFDEFINITION", "AcDbPdfDefinition", 1153, 0, 1}, {"PDFUNDERLAY", "AcDbPdfReference", 4095, 1, 2},
                              {"DGNDEFINITION", "AcDbDgnDefinition", 1153, 0, 1}, {"DGNUNDERLAY", "AcDbDgnReference", 4095, 1, 1}};
        for (const Want& w : wants) {
            const Rec* c = class_of(w.name);
            assert_true(c != nullptr, std::string(w.name) + ": a CLASS record");
            std::map<int, std::string> g;
            for (const Group& x : *c) g[x.first] = trim(x.second);
            assert_eq(std::string(w.cpp), g[2], std::string(w.name) + ": the C++ class");
            assert_eq(std::string("ObjectDBX Classes"), g[3], "the application");
            assert_eq(w.flags, std::atoi(g[90].c_str()), std::string(w.name) + ": the proxy flags");
            assert_eq(0, std::atoi(g[280].c_str()), "was a proxy: 0");
            assert_eq(w.entity, std::atoi(g[281].c_str()), std::string(w.name) + ": entity flag");
            if (v >= DxfVersion::R2004) assert_eq(w.count, std::atoi(g[91].c_str()), std::string(w.name) + ": the instance count");
            else assert_true(g.count(91) == 0, "no instance count before 2004");
        }
        assert_true(class_of("DWFUNDERLAY") == nullptr && class_of("DWFDEFINITION") == nullptr, "only the classes the drawing uses");
    }
}

TEST(reference_types_an_underlay_in_a_block_is_typed_and_the_inserts_transform_is_the_inserts) {
    for (const char* tag : kTags) {
        Drawing d = load(facit_bytes(underlay_file(tag, false, "in_block")), "in_block");
        const hdxf::Block* block = d.find_block("ULBLK");
        assert_true(block != nullptr, std::string(tag) + ": the block");
        assert_eq(1, static_cast<int>(block->entities.size()), "one entity in the block");
        assert_true(block->entities[0].type == EntityType::PdfUnderlay, "the underlay is typed in the block");
        const hdxf::UnderlayData& u = block->entities[0].as_underlay();
        assert_eq(std::string("D06"), u.definition_handle, "it still points to the definition");
        assert_true(u.scale.x == 2.0 && near(30.0, u.rotation, 1e-9), "the transforms it had when it was put in the block");
        // the INSERT of the block carries the new transform
        const Entity* insert = nullptr;
        for (const Entity& e : d.entities()) if (e.type == EntityType::Insert && e.as_insert().block_name == "ULBLK") insert = &e;
        assert_true(insert != nullptr, "the INSERT");
        assert_true(insert->as_insert().x_scale == 2.0 && insert->as_insert().z_scale == 2.0 && near(45.0, insert->as_insert().rotation, 1e-9) &&
                        insert->as_insert().insertion_point.x == 200.0,
                    "INSERT: scale 2, rotation 45 at x 200");
        // the definition's reactors name the underlay of the block
        const Object* def = d.find_object("D06");
        assert_true(def != nullptr && std::find(def->reactors.begin(), def->reactors.end(), block->entities[0].props.handle) != def->reactors.end(),
                    "the definition has the block's underlay as a reactor");
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "in_block");
        assert_true(out == facit_bytes(underlay_file(tag, false, "in_block")), "written unchanged");
    }
    // a new one: an underlay built in a block, its definition's reactor and the INSERT's extents (none: no source reader)
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2010, DxfVersion::R2018}) {
        Drawing d(v);
        Entity& model_level = d.add_underlay(UnderlayKind::Pdf, "a.pdf", "1", {0, 0, 0});
        Entity underlay = model_level;
        d.entities().clear();
        hdxf::Block& block = d.add_block("PLANS");
        block.entities.push_back(underlay);
        Entity& insert = d.add_insert("PLANS", {10, 20, 0});
        insert.as_insert().rotation = 45.0;
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "block");
        Drawing back = load(out, "block");
        const hdxf::Block* b = back.find_block("PLANS");
        assert_true(b != nullptr && b->entities.size() == 1 && b->entities[0].type == EntityType::PdfUnderlay, "the underlay stays in its block");
        auto defs = objects_of(back, ObjectType::PdfDefinition);
        assert_eq(1, static_cast<int>(defs.size()));
        assert_true(std::find(defs[0]->reactors.begin(), defs[0]->reactors.end(), b->entities[0].props.handle) != defs[0]->reactors.end(),
                    "the block's underlay is a reactor of the definition");
    }
}
