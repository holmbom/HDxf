#include "test_framework.hpp"
#include "assoc_test_support.hpp"
#include "type_version_matrix.hpp"
#include "TypedRecordTypes.hpp"
#include "dimension_block.hpp"
#include "hdxf.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

// Annotation contexts, DIMASSOC, ARC_DIMENSION and LARGE_RADIAL_DIMENSION of step 9.17 (plan 2026-10-27-fas-9-typer.md; B100-B107,
// B112-B116). The AutoCAD originals are doc/facit/acad_<version>_dxf_contexts*.dxf and _dim_special*.dxf (fas9_contexts.scr,
// fas9_special_dimensions.scr: AutoCAD 2025's core), Erik's acad_<version>_dxf_annotative*.dxf and _dimensions*.dxf. They are local
// files (not in git): a test whose file is missing is skipped.

using namespace hdxf;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

namespace {

void eq(int a, int b, const std::string& what) {
    if (a != b) throw hdxf_test::AssertFailure{what + ": " + std::to_string(a) + " is not " + std::to_string(b)};
}
void eq(size_t a, size_t b, const std::string& what) { eq(static_cast<int>(a), static_cast<int>(b), what); }
void eq(const std::string& a, const std::string& b, const std::string& what) {
    if (a != b) throw hdxf_test::AssertFailure{what + ": \"" + a + "\" is not \"" + b + "\""};
}
void near(double a, double b, const std::string& what, double eps = 1e-9) {
    if (!(std::fabs(a - b) <= eps * std::max(1.0, std::fabs(b)))) {
        throw hdxf_test::AssertFailure{what + ": " + std::to_string(a) + " is not " + std::to_string(b)};
    }
}
void near(const Point3& a, const Point3& b, const std::string& what, double eps = 1e-9) {
    near(a.x, b.x, what + " x", eps);
    near(a.y, b.y, what + " y", eps);
    near(a.z, b.z, what + " z", eps);
}

const char* const kTags[] = {"2000", "2004", "2007", "2010", "2013", "2018"};

std::filesystem::path facit(const std::string& tag, const std::string& theme, bool binary) {
    return hdxf_test::source_path("doc/facit/acad_" + tag + "_dxf_" + theme + (binary ? "_bin" : "") + ".dxf");
}

std::vector<uint8_t> facit_bytes(const std::string& tag, const std::string& theme, bool binary = false) {
    std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(facit(tag, theme, binary));
    if (bytes.empty()) hdxf_test::skip("doc/facit/acad_" + tag + "_dxf_" + theme + (binary ? "_bin" : "") + ".dxf is not here");
    return bytes;
}

Drawing load_facit(const std::string& tag, const std::string& theme, bool binary = false) {
    std::vector<uint8_t> bytes = facit_bytes(tag, theme, binary);
    Drawing d;
    assert_true(d.load_from_memory(bytes.data(), bytes.size()), "cannot read " + theme + ": " + d.last_error());
    return d;
}

bool is_context(ObjectType t) {
    switch (t) {
        case ObjectType::TextContext: case ObjectType::LeaderContext: case ObjectType::MTextContext:
        case ObjectType::MTextAttributeContext: case ObjectType::MLeaderContext: case ObjectType::BlkRefContext:
        case ObjectType::AlDimContext: case ObjectType::AngDimContext: case ObjectType::DmDimContext:
        case ObjectType::OrdDimContext: case ObjectType::RadimContext: case ObjectType::RadimLgContext:
        case ObjectType::FcfContext: case ObjectType::AnnotScaleContext: case ObjectType::HatchScaleContext:
        case ObjectType::HatchViewContext:
            return true;
        default:
            return false;
    }
}

const char* name_of(ObjectType t) {
    const internal::ObjectCodec* c = internal::TypedRegistry::instance().find_object(t);
    return c ? c->dxf_name.data() : "";
}

std::vector<const Object*> of_type(const Drawing& d, ObjectType t) {
    std::vector<const Object*> out;
    for (const Object& o : d.objects()) if (o.type == t) out.push_back(&o);
    return out;
}

size_t count_of(const Drawing& d, ObjectType t) { return of_type(d, t).size(); }

std::vector<const Entity*> entities_of(const Drawing& d, EntityType t) {
    std::vector<const Entity*> out;
    for (const Entity& e : d.entities()) if (e.type == t) out.push_back(&e);
    for (const Block& b : d.blocks()) for (const Entity& e : b.entities) if (e.type == t) out.push_back(&e);
    return out;
}

const ObjectType kContextTypes[] = {ObjectType::TextContext, ObjectType::LeaderContext, ObjectType::MTextContext,
                                    ObjectType::MTextAttributeContext, ObjectType::MLeaderContext, ObjectType::BlkRefContext,
                                    ObjectType::AlDimContext, ObjectType::AngDimContext, ObjectType::DmDimContext,
                                    ObjectType::OrdDimContext, ObjectType::RadimContext, ObjectType::RadimLgContext,
                                    ObjectType::FcfContext, ObjectType::AnnotScaleContext, ObjectType::HatchScaleContext,
                                    ObjectType::HatchViewContext};

// A context of every type with values that are not the defaults
ObjectData sample_data(ObjectType t) {
    ObjectContextBase base;
    base.version = 4;
    DimContextCommon dim;
    dim.block_name = "*D1";
    dim.text_location = {12.5, -3.25};
    dim.value_140 = 0.5;
    dim.flag_298 = true;
    HatchContextCommon hatch;
    HatchPatternLine line;
    line.angle = 45.0;
    line.base_point = {0.5, 0.25};
    line.offset = {-0.125, 0.125};
    line.dashes = {0.5, -0.25};
    hatch.pattern_lines = {line, HatchPatternLine{}};
    hatch.pattern_scale = 2.5;
    hatch.base_point = {1.0, 2.0, 0.0};
    switch (t) {
        case ObjectType::TextContext: {
            TextContextData d;
            d.base = base;
            d.flags = 2;
            d.rotation = 0.5;
            d.insertion = {1.0, 2.0};
            d.alignment = {3.0, 4.0};
            return Boxed<TextContextData>(d);
        }
        case ObjectType::LeaderContext: {
            LeaderContextData d;
            d.base = base;
            d.points = {{0, 0, 0}, {5, 5, 0}, {10, 5, 0}, {12, 6, 1}};
            d.direction = {1, 0, 0};
            d.offset = {0.5, 0.5, 0};
            d.annotation_offset = {1, 1, 0};
            return Boxed<LeaderContextData>(d);
        }
        case ObjectType::MTextContext: {
            MTextContextData d;
            d.base = base;
            d.location = {3, 4, 0};
            d.reference_width = 20.0;
            d.text_width = 7.5;
            d.text_height = 1.5;
            return Boxed<MTextContextData>(d);
        }
        case ObjectType::MTextAttributeContext: {
            MTextAttributeContextData d;
            d.base = base;
            d.rotation = 0.25;
            d.insertion = {1, 2};
            d.alignment = {3, 4};
            MTextContextData m;
            m.base = base;
            m.location = {1, 2, 0};
            m.text_width = 4.0;
            d.embedded = m;
            return Boxed<MTextAttributeContextData>(d);
        }
        case ObjectType::MLeaderContext: {
            MLeaderContextData d;
            d.base = base;
            d.content.scale = 2.0;
            d.content.text = "Note";
            d.content.base_point = {1, 2, 0};
            MLeaderLeader leader;
            leader.last_point = {3, 4, 0};
            MLeaderLine ml;
            ml.vertices = {{0, 0, 0}, {1, 1, 0}, {2, 1, 0}};
            leader.lines = {ml};
            d.content.leaders = {leader};
            return Boxed<MLeaderContextData>(d);
        }
        case ObjectType::BlkRefContext: {
            BlkRefContextData d;
            d.base = base;
            d.rotation = 0.5;
            d.insertion = {10, 20, 0};
            d.scale = {2, 3, 4};
            return Boxed<BlkRefContextData>(d);
        }
        case ObjectType::AlDimContext: { AlDimContextData d; d.base = base; d.dimension = dim; d.dimension_line_point = {7, 8, 0}; return Boxed<AlDimContextData>(d); }
        case ObjectType::AngDimContext: { AngDimContextData d; d.base = base; d.dimension = dim; d.arc_point = {7, 8, 0}; return Boxed<AngDimContextData>(d); }
        case ObjectType::DmDimContext: { DmDimContextData d; d.base = base; d.dimension = dim; d.point_11 = {7, 8, 0}; d.point_12 = {9, 1, 0}; return Boxed<DmDimContextData>(d); }
        case ObjectType::OrdDimContext: { OrdDimContextData d; d.base = base; d.dimension = dim; d.point_11 = {7, 8, 0}; d.point_12 = {9, 1, 0}; return Boxed<OrdDimContextData>(d); }
        case ObjectType::RadimContext: { RadimContextData d; d.base = base; d.dimension = dim; d.point_11 = {7, 8, 0}; return Boxed<RadimContextData>(d); }
        case ObjectType::RadimLgContext: { RadimLgContextData d; d.base = base; d.dimension = dim; d.point_11 = {7, 8, 0}; d.point_12 = {9, 1, 0}; d.point_13 = {2, 2, 2}; return Boxed<RadimLgContextData>(d); }
        case ObjectType::FcfContext: { FcfContextData d; d.base = base; d.location = {1, 2, 3}; return Boxed<FcfContextData>(d); }
        case ObjectType::AnnotScaleContext: { AnnotScaleContextData d; d.base = base; return Boxed<AnnotScaleContextData>(d); }
        case ObjectType::HatchScaleContext: { HatchScaleContextData d; d.base = base; d.hatch = hatch; return Boxed<HatchScaleContextData>(d); }
        case ObjectType::HatchViewContext: { HatchViewContextData d; d.base = base; d.hatch = hatch; d.value_51 = 0.75; return Boxed<HatchViewContextData>(d); }
        default: break;
    }
    return ObjectData{};
}

// The scale list of a new drawing: the SCALE "1:1" when it has one; one is added when it has not
std::string scale_handle(Drawing& d, const std::string& name) {
    if (const Object* s = d.find_scale(name)) return s->handle;
    return {};
}

} // namespace

// The 16 types are in the registry with a codec and a matrix that says something
TEST(ctx_codecs_are_registered) {
    for (ObjectType t : kContextTypes) {
        const internal::ObjectCodec* c = internal::TypedRegistry::instance().find_object(t);
        assert_true(c != nullptr, std::string("no codec for type ") + std::to_string(static_cast<int>(t)));
        assert_true(c->read && c->write && c->references, std::string(c->dxf_name) + ": read, write and references");
        eq(c->step, 17, std::string(c->dxf_name) + " step");
        assert_true(c->versions.first_version().has_value(), std::string(c->dxf_name) + ": a matrix");
    }
    assert_true(internal::TypedRegistry::instance().find_object("ACDB_RADIMLGOBJECTCONTEXTDATA_CLASS") ==
                    internal::TypedRegistry::instance().find_object("ACDB_RADIMLRGOBJECTCONTEXTDATA_CLASS"),
                "the plan's name for the large radial context is AutoCAD's");
    for (const char* n : {"DIMASSOC"}) assert_true(internal::TypedRegistry::instance().find_object(n) != nullptr, n);
    for (const char* n : {"ARC_DIMENSION", "LARGE_RADIAL_DIMENSION"}) assert_true(internal::TypedRegistry::instance().find_entity(n) != nullptr, n);
}

// Every context of AutoCAD's own files, in every version and both formats, is typed (no Unknown object is left of the 15 names that
// AutoCAD writes) and its values are the file's
TEST(ctx_every_context_of_autocads_files_is_typed) {
    int checked = 0;
    for (const char* theme : {"contexts", "annotative"}) {
        for (const char* tag : kTags) {
            for (bool binary : {false, true}) {
                if (!std::filesystem::exists(facit(tag, theme, binary))) continue;
                Drawing d = load_facit(tag, theme, binary);
                for (const Object& o : d.objects()) {
                    if (o.type != ObjectType::Unknown) continue;
                    const std::string& n = o.as_unknown().object_type;
                    assert_true(n.find("CONTEXTDATA") == std::string::npos, std::string(theme) + " " + tag + ": " + n + " " + o.handle + " is untyped");
                }
                for (const std::string& w : d.warnings()) {
                    assert_true(w.find("CONTEXTDATA") == std::string::npos, std::string(theme) + " " + tag + ": " + w);
                }
                ++checked;
            }
        }
    }
    if (checked == 0) hdxf_test::skip("no context facit");
}

TEST(ctx_values_of_autocads_text_mtext_dimension_and_hatch_contexts) {
    Drawing d = load_facit("2018", "annotative");
    // The first TEXT context: 70 4, default, the 1:1 scale; 70 0, 50, two points of two coordinates
    const Object* text = nullptr;
    for (const Object* o : of_type(d, ObjectType::TextContext)) if (o->handle == "43F") text = o;
    assert_true(text != nullptr, "TEXT context 43F");
    const TextContextData& t = text->as_text_context();
    eq(t.base.version, 4, "base 70");
    assert_true(t.base.is_default, "default");
    eq(t.base.scale, "B7", "340");
    eq(t.flags, 0, "text 70 is not the base's");
    near(t.rotation, 0.0, "50");
    // MTEXT: the drawing has two MTEXT entities, each with three contexts (1:1, 1:2, 1:5, in file order); the widths follow the scale
    std::vector<const Object*> mtext = of_type(d, ObjectType::MTextContext);
    eq(mtext.size(), static_cast<size_t>(6), "MTEXT contexts");
    eq(mtext[0]->owner, mtext[2]->owner, "the first three belong to one MTEXT");
    assert_true(mtext[0]->owner != mtext[3]->owner, "the last three belong to the other");
    near(mtext[3]->as_mtext_context().reference_width, 40.0, "second MTEXT 40 at 1:1");
    near(mtext[5]->as_mtext_context().reference_width, 200.0, "second MTEXT 40 at 1:5");
    near(mtext[0]->as_mtext_context().reference_width, 60.0, "40 at 1:1");
    near(mtext[1]->as_mtext_context().reference_width, 120.0, "40 at 1:2");
    near(mtext[2]->as_mtext_context().reference_width, 300.0, "40 at 1:5");
    near(mtext[0]->as_mtext_context().text_width, 2.339154160982265, "42");
    near(mtext[0]->as_mtext_context().location, {0, 20, 0}, "11");
    // The dimension contexts: the block the dimension has at that scale, the text's middle
    const Object* al = of_type(d, ObjectType::AlDimContext).front();
    eq(al->as_aldim_context().dimension.block_name.substr(0, 2), "*D", "ALDIM block");
    near(al->as_aldim_context().dimension.text_location.x, 40.0, "ALDIM 10");
    near(al->as_aldim_context().dimension_line_point.x, 80.0, "ALDIM 11");
    // The hatch contexts: the pattern lines, the scale doubles
    std::vector<const Object*> hs = of_type(d, ObjectType::HatchScaleContext);
    eq(hs.size(), static_cast<size_t>(3), "HATCHSCALE");
    eq(hs[0]->as_hatch_scale_context().hatch.pattern_lines.size(), static_cast<size_t>(1), "78");
    near(hs[0]->as_hatch_scale_context().hatch.pattern_lines[0].angle, 45.0, "53");
    near(hs[1]->as_hatch_scale_context().hatch.pattern_scale, 2.0, "40 at 1:2");
    std::vector<const Object*> hv = of_type(d, ObjectType::HatchViewContext);
    eq(hv[0]->as_hatch_view_context().pointer_330, "0", "330");
    // BLKREF: the scale in 41, 42, 43; MLEADER: the content scale; the MTEXT attribute holds an MTEXT context (101)
    std::vector<const Object*> br = of_type(d, ObjectType::BlkRefContext);
    near(br[2]->as_blkref_context().scale.x, 5.0, "41 at 1:5");
    std::vector<const Object*> ml = of_type(d, ObjectType::MLeaderContext);
    near(ml[1]->as_mleader_context().content.scale, 2.0, "MLEADER 40 at 1:2");
    eq(ml[0]->as_mleader_context().content.text, "Annotative MLEADER", "MLEADER text");
    eq(ml[0]->as_mleader_context().content.leaders.size(), static_cast<size_t>(1), "MLEADER leaders");
    std::vector<const Object*> ma = of_type(d, ObjectType::MTextAttributeContext);
    eq(ma.size(), static_cast<size_t>(3), "MTEXTATTRIBUTE");
    assert_true(ma[0]->as_mtext_attribute_context().embedded.has_value(), "its embedded MTEXT context");
    // The LEADER context's count is the number of its points, not a handle
    Drawing c = load_facit("2018", "contexts");
    std::vector<const Object*> ld = of_type(c, ObjectType::LeaderContext);
    eq(ld.size(), static_cast<size_t>(3), "LEADER contexts");
    assert_true(!ld[0]->as_leader_context().points.empty(), "a leader has points");
}

// A file read and written again is the file; written from the typed values alone, every context is AutoCAD's own record
TEST(ctx_written_unchanged_and_from_typed_values_is_autocads_record) {
    int compared = 0;
    for (const char* theme : {"contexts", "annotative"}) {
        for (const char* tag : kTags) {
            for (bool binary : {false, true}) {
                if (!std::filesystem::exists(facit(tag, theme, binary))) continue;
                std::vector<uint8_t> original = assoc_test::as_ascii(facit_bytes(tag, theme, binary));
                Drawing d = load_facit(tag, theme, binary);
                std::vector<uint8_t> unchanged = d.save_to_memory(DxfFormat::Ascii);
                assert_true(!unchanged.empty(), d.last_error());
                for (const Object& o : d.objects()) {
                    if (!is_context(o.type)) continue;
                    std::string a = assoc_test::ascii_record(original, name_of(o.type), o.handle);
                    std::string b = assoc_test::ascii_record(unchanged, name_of(o.type), o.handle);
                    assert_true(!a.empty(), std::string(name_of(o.type)) + " " + o.handle + " is in the original");
                    eq(assoc_test::record_difference(a, b), "", std::string(theme) + " " + tag + " unchanged " + name_of(o.type) + " " + o.handle);
                }
                for (Object& o : d.objects()) if (is_context(o.type)) o.raw = RawRecord();
                // A binary file is compared as text through the same reading and writing (as the other facit tests do): its numbers have no width there
                std::vector<uint8_t> typed = binary ? assoc_test::as_ascii(d.save_to_memory(DxfFormat::Binary)) : d.save_to_memory(DxfFormat::Ascii);
                assert_true(!typed.empty(), d.last_error());
                for (const Object& o : d.objects()) {
                    if (!is_context(o.type)) continue;
                    std::string a = assoc_test::ascii_record(original, name_of(o.type), o.handle);
                    std::string b = assoc_test::ascii_record(typed, name_of(o.type), o.handle);
                    eq(assoc_test::record_difference(a, b), "", std::string(theme) + " " + tag + " typed " + name_of(o.type) + " " + o.handle);
                    ++compared;
                }
            }
        }
    }
    if (compared == 0) hdxf_test::skip("no context facit");
}

// Nine versions x two formats for each of the 16 types: a new context in a new drawing, read back, written unchanged, in the other
// format; before 2000 the named error before anything is written
TEST(ctx_version_matrix_for_every_type) {
    int generated = 0;
    for (ObjectType t : kContextTypes) {
        const internal::ObjectCodec* codec = internal::TypedRegistry::instance().find_object(t);
        const std::string name = codec->dxf_name.data();
        auto add = [&](Drawing& d) {
            Entity& text = d.add_text({0, 0, 0}, "Note", 2.5);
            const std::string handle = d.entity_handle(text);
            if (d.version() >= DxfVersion::R2000) {
                d.add_annotation_scale("1:1", 1.0, 1.0);
                d.add_object_context(handle, "1:1", t, sample_data(t));
                return;
            }
            // Before 2000 there is nowhere to put it: the object is added as a program that built it by hand would
            Object o;
            o.type = t;
            o.handle = "ABC1";
            o.owner = "0";
            o.data = sample_data(t);
            d.objects().push_back(std::move(o));
            (void)handle;
        };
        auto verify = [&](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            std::vector<const Object*> found = of_type(r, t);
            if (found.size() != 1) return name + ": " + std::to_string(found.size()) + " records";
            std::vector<const Entity*> texts = entities_of(r, EntityType::Text);
            if (texts.empty()) return "no text";
            std::vector<const Object*> own = r.object_contexts(texts.front()->props.handle);
            if (own.size() != 1 || own[0] != found[0]) return name + ": not in the entity's context dictionary";
            const ObjectContextBase* b = found[0]->context_base();
            if (!b || b->version != 4 || !b->is_default || b->scale.empty()) return name + ": base";
            if (!r.find_scale(b->scale)) return name + ": the scale is not there";
            return {};
        };
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix(name, codec->versions, add, verify);
        generated += report.generated;
    }
    assert_true(generated > 0, "nothing was generated");
}
