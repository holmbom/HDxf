#include "test_framework.hpp"
#include "assoc_test_support.hpp"
#include "type_version_matrix.hpp"
#include "TypedRecordTypes.hpp"
#include "dimension_block.hpp"
#include "extents.hpp"
#include "text_layout.hpp"
#include "hdxf.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

// Relations of step 9.17 (plan 2026-10-27-fas-9-typer.md; B19, B104, B112): the scale list, the entity's context dictionaries, the
// contexts and their removal; DIMASSOC's four points, masks and reactors; the special dimensions' blocks, extents and versions.
// AutoCAD's originals: doc/facit/acad_<version>_dxf_contexts*.dxf, _dim_special*.dxf, Erik's _annotative*.dxf, _dimensions*.dxf
// (local files: a test whose file is missing is skipped).

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
bool close(double a, double b, double eps) { return std::fabs(a - b) <= eps * std::max(1.0, std::fabs(b)); }
void near(double a, double b, const std::string& what, double eps = 1e-9) {
    if (!close(a, b, eps)) throw hdxf_test::AssertFailure{what + ": " + std::to_string(a) + " is not " + std::to_string(b)};
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

Drawing reload(Drawing& d, DxfFormat f = DxfFormat::Ascii) {
    std::vector<uint8_t> bytes = d.save_to_memory(f);
    assert_true(!bytes.empty(), "cannot be written: " + d.last_error());
    return assoc_test::reload(bytes);
}

std::vector<const Object*> of_type(const Drawing& d, ObjectType t) {
    std::vector<const Object*> out;
    for (const Object& o : d.objects()) if (o.type == t) out.push_back(&o);
    return out;
}

std::vector<const Entity*> all_entities(const Drawing& d) {
    std::vector<const Entity*> out;
    for (const Entity& e : d.entities()) out.push_back(&e);
    for (const Block& b : d.blocks()) for (const Entity& e : b.entities) out.push_back(&e);
    return out;
}

std::vector<const Entity*> entities_of(const Drawing& d, EntityType t) {
    std::vector<const Entity*> out;
    for (const Entity* e : all_entities(d)) if (e->type == t) out.push_back(e);
    return out;
}

size_t contexts_in(const Drawing& d) {
    size_t n = 0;
    for (const Object& o : d.objects()) if (o.context_base()) ++n;
    return n;
}

std::string slot_string(const DimAssocData& a) {
    std::string s;
    for (const auto& p : a.points) s += p ? '1' : '0';
    return s;
}

// AutoCAD's own fonts (SHX), where a dimension's text is measured; skipped when they are not here
struct AutoCadFonts {
    std::vector<std::filesystem::path> saved = hdxf::font_paths();
    AutoCadFonts() {
        std::filesystem::path fonts = "C:/Program Files/Autodesk/AutoCAD 2025/Fonts";
        if (!std::filesystem::exists(fonts / "txt.shx")) {
            hdxf_test::skip("AutoCAD's Fonts folder is not here");
        }
        std::vector<std::filesystem::path> paths = saved;
        paths.insert(paths.begin(), fonts);
        hdxf::set_font_paths(paths);
    }
    ~AutoCadFonts() { hdxf::set_font_paths(saved); }
};

} // namespace

// ---------------------------------------------------------------------------
// The contexts and their scales
// ---------------------------------------------------------------------------

TEST(ctx_object_contexts_follow_the_entitys_dictionary_chain) {
    Drawing d = load_facit("2018", "contexts");
    const Entity* text = nullptr;
    for (const Entity* e : entities_of(d, EntityType::Text)) text = e;
    assert_true(text != nullptr, "a TEXT");
    std::vector<const Object*> contexts = d.object_contexts(text->props.handle);
    eq(contexts.size(), static_cast<size_t>(3), "the annotative TEXT has three contexts");
    eq(static_cast<int>(contexts[0]->type), static_cast<int>(ObjectType::TextContext), "their type");
    assert_true(contexts[0]->context_base()->is_default, "the first is the default");
    assert_false(contexts[1]->context_base()->is_default, "the others are not");
    // The owner of a context is the *A dictionary, which the extension dictionary chain leads to
    const Object* dict = d.find_object(contexts[0]->owner);
    assert_true(dict && dict->type == ObjectType::Dictionary, "the owner is a dictionary");
    // The scales the contexts point at are in the scale list, one each
    std::set<std::string> scales;
    for (const Object* c : contexts) {
        const Object* s = d.find_scale(c->context_base()->scale);
        assert_true(s != nullptr, "the scale of " + c->handle);
        scales.insert(s->as_scale().name);
        assert_true(d.object_context(text->props.handle, s->handle) == c, "the context of the scale");
    }
    eq(scales.size(), static_cast<size_t>(3), "three scales");
    assert_true(scales.count("1:1") && scales.count("HDXF_1_2") && scales.count("HDXF_1_5"), "1:1, 1:2 and 1:5");
}

// -OBJECTSCALE _Delete (AutoCAD's own file: annotative_scale_removed): the context of one scale goes with its dictionary entry; the
// other contexts, the scale and every number stay
TEST(ctx_removing_one_context_keeps_the_rest_and_every_number) {
    Drawing d = load_facit("2018", "contexts");
    const size_t before = contexts_in(d);
    size_t leaders_points = 0;
    for (const Object* o : of_type(d, ObjectType::LeaderContext)) leaders_points += o->as_leader_context().points.size();
    const Object* five = d.find_scale("HDXF_1_5");
    assert_true(five != nullptr, "the 1:5 scale");
    const std::string scale = five->handle;
    size_t removed = 0;
    for (const Entity* e : all_entities(d)) {
        if (!d.object_contexts(e->props.handle).empty() && d.object_context(e->props.handle, scale)) {
            assert_true(d.remove_object_context(e->props.handle, scale), "remove");
            ++removed;
        }
    }
    assert_true(removed > 0, "some entity had a 1:5 context");
    eq(contexts_in(d), before - removed, "one context per entity less");
    assert_true(d.find_scale("HDXF_1_5") != nullptr, "the scale is still in the list");
    Drawing r = reload(d);
    eq(contexts_in(r), before - removed, "after a save");
    size_t after_points = 0;
    for (const Object* o : of_type(r, ObjectType::LeaderContext)) after_points += o->as_leader_context().points.size();
    // A LEADER context less: its vertices go with it, the others' vertex counts are what they were
    const size_t per_context = of_type(d, ObjectType::LeaderContext).empty() ? 0 : of_type(d, ObjectType::LeaderContext)[0]->as_leader_context().points.size();
    eq(after_points, leaders_points - per_context, "vertex counts");
    // The dictionary entries of the removed contexts are gone: none leads nowhere
    for (const Object& o : r.objects()) {
        if (o.type != ObjectType::Dictionary) continue;
        for (const DictionaryEntry& e : o.as_dictionary().entries) {
            if (e.name.size() > 2 && e.name[0] == '*' && e.name[1] == 'A') assert_true(r.find_object(e.handle) != nullptr, "entry " + e.name);
        }
    }
}

TEST(ctx_removing_a_scale_removes_its_contexts_everywhere) {
    Drawing d = load_facit("2018", "contexts");
    const size_t before = contexts_in(d);
    const Object* two = d.find_scale("HDXF_1_2");
    assert_true(two != nullptr, "the 1:2 scale");
    size_t per_scale = 0;
    for (const Object& o : d.objects()) {
        if (o.context_base() && o.context_base()->scale == two->handle) ++per_scale;
    }
    assert_true(per_scale > 0, "contexts at 1:2");
    assert_true(d.remove_annotation_scale("HDXF_1_2"), "remove");
    assert_true(d.find_scale("HDXF_1_2") == nullptr, "the scale is gone");
    eq(contexts_in(d), before - per_scale, "its contexts are gone");
    Drawing r = reload(d);
    eq(contexts_in(r), before - per_scale, "after a save");
    assert_false(d.remove_annotation_scale("HDXF_1_2"), "twice");
}

// The scale object erased by hand: the pointer is 0, nothing else of a context changes (B19: no number is cleared)
TEST(ctx_an_erased_scale_object_clears_only_the_pointer) {
    Drawing d = load_facit("2018", "contexts");
    const Object* five = d.find_scale("HDXF_1_5");
    assert_true(five != nullptr, "1:5");
    const std::string handle = five->handle;
    std::map<std::string, size_t> points;
    for (const Object* o : of_type(d, ObjectType::LeaderContext)) points[o->handle] = o->as_leader_context().points.size();
    std::map<std::string, int> flags;
    for (const Object* o : of_type(d, ObjectType::TextContext)) flags[o->handle] = o->as_text_context().flags;
    std::vector<Object>& objects = d.objects();
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        if (it->handle == handle) { objects.erase(it); break; }
    }
    Drawing r = reload(d);
    size_t zeroed = 0;
    for (const Object& o : r.objects()) {
        const ObjectContextBase* b = o.context_base();
        if (b && (b->scale == "0" || b->scale.empty())) ++zeroed;
    }
    assert_true(zeroed > 0, "the contexts that pointed at the scale point at nothing");
    for (const Object* o : of_type(r, ObjectType::LeaderContext)) {
        assert_true(points.count(o->handle) > 0, "the same LEADER context");
        eq(o->as_leader_context().points.size(), points[o->handle], "vertex count of " + o->handle);
    }
    for (const Object* o : of_type(r, ObjectType::TextContext)) eq(o->as_text_context().flags, flags[o->handle], "70 of " + o->handle);
}

TEST(ctx_an_erased_entity_takes_its_contexts_with_it) {
    Drawing d = load_facit("2018", "contexts");
    const size_t before = contexts_in(d);
    std::vector<Entity>& entities = d.entities();
    size_t doomed = 0;
    for (auto it = entities.begin(); it != entities.end(); ++it) {
        if (it->type == EntityType::Text && !d.object_contexts(it->props.handle).empty()) {
            doomed = d.object_contexts(it->props.handle).size();
            entities.erase(it);
            break;
        }
    }
    assert_true(doomed == 3, "an annotative TEXT with three contexts");
    Drawing r = reload(d);
    eq(contexts_in(r), before - doomed, "its contexts, the dictionaries and nothing else");
}

TEST(ctx_new_contexts_in_a_new_drawing_and_a_second_scale) {
    Drawing d(DxfVersion::R2018);
    Entity& text = d.add_text({0, 0, 0}, "Note", 2.5);
    const std::string handle = d.entity_handle(text);
    const Object* one = d.add_annotation_scale("1:1", 1.0, 1.0);
    const Object* two = d.add_annotation_scale("1:2", 1.0, 2.0);
    assert_true(one && two && one->as_scale().unit_scale && !two->as_scale().unit_scale, "two scales");
    TextContextData a;
    a.insertion = {1, 2};
    TextContextData b = a;
    b.rotation = 0.5;
    assert_true(d.add_object_context(handle, "1:1", ObjectType::TextContext, Boxed<TextContextData>(a)) != nullptr, "first");
    assert_true(d.add_object_context(handle, "1:2", ObjectType::TextContext, Boxed<TextContextData>(b)) != nullptr, "second");
    std::vector<const Object*> contexts = d.object_contexts(handle);
    eq(contexts.size(), static_cast<size_t>(2), "two contexts");
    assert_true(contexts[0]->context_base()->is_default && !contexts[1]->context_base()->is_default, "the first is the default");
    assert_true(d.add_object_context("FFFF", "1:1", ObjectType::TextContext, Boxed<TextContextData>(a)) == nullptr, "no such entity");
    assert_true(d.add_object_context(handle, "9:9", ObjectType::TextContext, Boxed<TextContextData>(a)) == nullptr, "no such scale");
    assert_true(d.add_object_context(handle, "1:1", ObjectType::Scale, ScaleData{}) == nullptr, "not a context type");
    // Replacing the context of a scale
    TextContextData c = a;
    c.flags = 7;
    d.add_object_context(handle, "1:2", ObjectType::TextContext, Boxed<TextContextData>(c));
    eq(d.object_contexts(handle).size(), static_cast<size_t>(2), "still two");
    Drawing r = reload(d);
    const Entity* rt = entities_of(r, EntityType::Text).front();
    std::vector<const Object*> rc = r.object_contexts(rt->props.handle);
    eq(rc.size(), static_cast<size_t>(2), "after a save");
    eq(rc[1]->as_text_context().flags, 7, "the replaced one");
    // The entity says it is annotative: AutoCAD's XDATA
    bool annotative = false;
    for (const XdataList& x : rt->props.xdata) annotative = annotative || x.app_name == "AcadAnnotative";
    assert_true(annotative, "AcadAnnotative XDATA");
    // Saved before 2000
    Drawing old(DxfVersion::R2018);
    old.set_version(DxfVersion::R2000);
    assert_true(!old.save_to_memory(DxfFormat::Ascii).empty(), "empty is fine");
}

// ---------------------------------------------------------------------------
// DIMASSOC
// ---------------------------------------------------------------------------

TEST(dimassoc_four_points_masks_and_values_of_autocads_files) {
    int checked = 0;
    for (const char* theme : {"dimensions", "dim_special"}) {
        for (const char* tag : kTags) {
            for (bool binary : {false, true}) {
                if (!std::filesystem::exists(facit(tag, theme, binary))) continue;
                Drawing d = load_facit(tag, theme, binary);
                std::vector<const Object*> assocs = of_type(d, ObjectType::DimAssoc);
                for (const Object* o : assocs) {
                    const DimAssocData& a = o->as_dim_assoc();
                    int mask = 0;
                    for (size_t i = 0; i < 4; ++i) if (a.points[i]) mask |= 1 << i;
                    eq(a.flags & 15, mask, "the mask of " + o->handle + " says which points there are");
                    for (const auto& p : a.points) {
                        if (!p) continue;
                        eq(p->class_name, "AcDbOsnapPointRef", "class");
                        assert_true(!p->main_handles.empty(), "a main object");
                        for (const std::string& h : p->main_handles) assert_true(d.find_entity(h) != nullptr, "the object " + h);
                    }
                    assert_true(d.find_entity(a.dimension) != nullptr, "the dimension " + a.dimension);
                    assert_true(d.dimension_association(a.dimension) == o, "found from the dimension");
                    ++checked;
                }
                for (const std::string& w : d.warnings()) assert_true(w.find("DIMASSOC") == std::string::npos, w);
            }
        }
    }
    if (checked == 0) hdxf_test::skip("no DIMASSOC facit");
    // The 2018 linear dimension has two points, the angular one all four, a radius one point (AutoCAD's masks 3, 15 and 2)
    Drawing d = load_facit("2018", "dim_special");
    std::set<int> masks;
    for (const Object* o : of_type(d, ObjectType::DimAssoc)) masks.insert(o->as_dim_assoc().flags);
    assert_true(masks.count(3) && masks.count(15) && masks.count(2) && masks.count(7), "masks 3, 15, 2 and 7");
}

TEST(dimassoc_written_unchanged_and_from_typed_values_is_autocads_record) {
    int compared = 0;
    for (const char* theme : {"dimensions", "dim_special"}) {
        for (const char* tag : kTags) {
            for (bool binary : {false, true}) {
                if (!std::filesystem::exists(facit(tag, theme, binary))) continue;
                std::vector<uint8_t> original = assoc_test::as_ascii(facit_bytes(tag, theme, binary));
                Drawing d = load_facit(tag, theme, binary);
                for (Object& o : d.objects()) if (o.type == ObjectType::DimAssoc) o.raw = RawRecord();
                // A binary file is compared as text through the same reading and writing (as the other facit tests do): its numbers have no width there
                std::vector<uint8_t> typed = binary ? assoc_test::as_ascii(d.save_to_memory(DxfFormat::Binary)) : d.save_to_memory(DxfFormat::Ascii);
                assert_true(!typed.empty(), d.last_error());
                for (const Object* o : of_type(d, ObjectType::DimAssoc)) {
                    std::string a = assoc_test::ascii_record(original, "DIMASSOC", o->handle);
                    std::string b = assoc_test::ascii_record(typed, "DIMASSOC", o->handle);
                    assert_true(!a.empty(), "DIMASSOC " + o->handle + " in the original");
                    eq(assoc_test::record_difference(a, b), "", std::string(theme) + " " + tag + " DIMASSOC " + o->handle);
                    ++compared;
                }
            }
        }
    }
    if (compared == 0) hdxf_test::skip("no DIMASSOC facit");
}

// The dimension and the geometry are reactors of the DIMASSOC (AutoCAD's own files); the DIMASSOC is in the dimension's extension
// dictionary under ACAD_DIMASSOC
TEST(dimassoc_reactors_and_the_dimensions_dictionary) {
    Drawing d = load_facit("2018", "dim_special");
    Drawing r = reload(d);
    for (const Object* o : of_type(r, ObjectType::DimAssoc)) {
        const DimAssocData& a = o->as_dim_assoc();
        const Entity* dim = r.find_entity(a.dimension);
        assert_true(dim != nullptr, "dimension");
        assert_true(std::find(dim->props.reactors.begin(), dim->props.reactors.end(), o->handle) != dim->props.reactors.end(),
                    "the dimension has " + o->handle + " as a reactor");
        for (const auto& p : a.points) {
            if (!p) continue;
            for (const std::string& h : p->main_handles) {
                const Entity* g = r.find_entity(h);
                assert_true(std::find(g->props.reactors.begin(), g->props.reactors.end(), o->handle) != g->props.reactors.end(),
                            "the geometry " + h + " has " + o->handle + " as a reactor");
            }
        }
        const Object* dict = r.find_object(dim->props.xdictionary);
        assert_true(dict && dict->type == ObjectType::Dictionary, "the extension dictionary");
        eq(o->owner, dict->handle, "the DIMASSOC's owner");
    }
}

// Geometry erased: the DIMASSOC loses the points of it (its mask follows); one that has none left is gone, the dimension stays
TEST(dimassoc_erased_geometry_drops_points_then_the_record) {
    Drawing d = load_facit("2018", "dim_special");
    const size_t dims = entities_of(d, EntityType::Dimension).size() + entities_of(d, EntityType::ArcDimension).size() +
                        entities_of(d, EntityType::LargeRadialDimension).size();
    const size_t assocs = of_type(d, ObjectType::DimAssoc).size();
    // The circle of a radius dimension: its DIMASSOC has that one point only
    std::string circle;
    for (const Object* o : of_type(d, ObjectType::DimAssoc)) {
        const DimAssocData& a = o->as_dim_assoc();
        if (slot_string(a) != "0100") continue;
        const Entity* g = d.find_entity(a.points[1]->main_handles.front());
        if (g && g->type == EntityType::Circle) { circle = g->props.handle; break; }
    }
    assert_true(!circle.empty(), "a circle that a dimension measures");
    std::vector<Entity>& entities = d.entities();
    for (auto it = entities.begin(); it != entities.end(); ++it) {
        if (it->props.handle == circle) { entities.erase(it); break; }
    }
    // Every DIMASSOC whose geometry is only that circle goes: AutoCAD's file measures it twice (a radius dimension and a large
    // radial one), so two records
    size_t measuring_the_circle = 0;
    for (const Object* o : of_type(d, ObjectType::DimAssoc)) {
        bool only = true, any = false;
        for (const auto& p : o->as_dim_assoc().points) {
            if (!p) continue;
            for (const std::string& h : p->main_handles) { any = true; only = only && h == circle; }
        }
        if (any && only) ++measuring_the_circle;
    }
    assert_true(measuring_the_circle >= 1, "a DIMASSOC of the circle");
    Drawing r = reload(d);
    eq(of_type(r, ObjectType::DimAssoc).size(), assocs - measuring_the_circle, "its DIMASSOCs are gone");
    eq(entities_of(r, EntityType::Dimension).size() + entities_of(r, EntityType::ArcDimension).size() +
           entities_of(r, EntityType::LargeRadialDimension).size(),
       dims, "the dimensions stay");
    // A LINE that two points of an angular dimension use: both go, the mask follows
    for (const Object* o : of_type(d, ObjectType::DimAssoc)) {
        const DimAssocData& a = o->as_dim_assoc();
        if (a.flags != 15) continue;
        const std::string line = a.points[0]->main_handles.front();
        const std::string dimension = a.dimension;
        Drawing e = load_facit("2018", "dim_special");
        std::vector<Entity>& list = e.entities();
        for (auto it = list.begin(); it != list.end(); ++it) {
            if (it->props.handle == line) { list.erase(it); break; }
        }
        Drawing back = reload(e);
        const Object* kept = back.dimension_association(dimension);
        assert_true(kept != nullptr, "the angular dimension still has the other line");
        eq(kept->as_dim_assoc().flags & 15, 12, "the mask of the two points that are left");
        break;
    }
}

TEST(dimassoc_api_adds_replaces_and_removes) {
    Drawing d(DxfVersion::R2018);
    Entity& line = d.add_line({0, 0, 0}, {80, 0, 0});
    const std::string g = d.entity_handle(line);
    Entity& dim = d.add_dimension(DimensionType::Rotated, {80, -20, 0}, {40, -20, 0});
    dim.as_dimension().def_point2 = {0, 0, 0};
    dim.as_dimension().def_point3 = {80, 0, 0};
    const std::string h = d.entity_handle(dim);
    DimAssocData a;
    DimAssocPointRef p;
    p.osnap_type = 1;
    p.main_handles = {g};
    p.main_subentity_type = 2;
    a.points[0] = p;
    DimAssocPointRef q = p;
    q.osnap_type = 13;
    q.near_parameter = 1.0;
    q.point = {0, 0, 2.0e50};
    a.points[3] = q;                                // the first and the fourth slot: the mask is 9
    const Object* made = d.add_dimension_association(h, a);
    assert_true(made != nullptr, "added");
    eq(made->as_dim_assoc().flags, 9, "the mask follows the slots (1 and 8)");
    assert_true(d.dimension_association(h) == made, "found");
    assert_true(d.add_dimension_association("FFFF", a) == nullptr, "no such dimension");
    Drawing r = reload(d);
    const Entity* rd = entities_of(r, EntityType::Dimension).front();
    const Object* ra = r.dimension_association(rd->props.handle);
    assert_true(ra != nullptr, "after a save");
    eq(slot_string(ra->as_dim_assoc()), "1001", "slots 1 and 4");
    eq(ra->as_dim_assoc().points[3]->osnap_type, 13, "72");
    near(ra->as_dim_assoc().points[3]->point.z, 2.0e50, "the z of 2E50 is kept", 1e-12);
    const Entity* rl = entities_of(r, EntityType::Line).front();
    assert_true(!rl->props.reactors.empty(), "the line has the DIMASSOC as a reactor");
    Drawing again = reload(r);
    assert_true(again.remove_dimension_association(entities_of(again, EntityType::Dimension).front()->props.handle), "removed");
    assert_true(of_type(again, ObjectType::DimAssoc).empty(), "none left");
    Drawing last = reload(again);
    assert_true(of_type(last, ObjectType::DimAssoc).empty(), "none after a save");
}

// ---------------------------------------------------------------------------
// ARC_DIMENSION and LARGE_RADIAL_DIMENSION
// ---------------------------------------------------------------------------

TEST(special_dimensions_of_autocads_files_are_typed_with_their_fields) {
    int checked = 0;
    for (const char* theme : {"dim_special", "dimensions", "annotative"}) {
        for (const char* tag : kTags) {
            for (bool binary : {false, true}) {
                if (!std::filesystem::exists(facit(tag, theme, binary))) continue;
                Drawing d = load_facit(tag, theme, binary);
                for (const Block& b : d.blocks()) {
                    for (const Entity& e : b.entities) {
                        assert_true(e.type != EntityType::Unknown || e.as_unknown().entity_type.find("DIMENSION") == std::string::npos,
                                    "a special dimension is untyped");
                    }
                }
                for (const Entity& e : d.entities()) {
                    if (e.type == EntityType::Unknown) {
                        assert_true(e.as_unknown().entity_type != "ARC_DIMENSION" && e.as_unknown().entity_type != "LARGE_RADIAL_DIMENSION",
                                    std::string(theme) + " " + tag + ": a special dimension is untyped");
                    }
                }
                ++checked;
            }
        }
    }
    if (checked == 0) hdxf_test::skip("no dimension facit");
    Drawing d = load_facit("2018", "dim_special");
    std::vector<const Entity*> arcs = entities_of(d, EntityType::ArcDimension);
    eq(arcs.size(), static_cast<size_t>(2), "two ARC_DIMENSIONs");
    const ArcDimensionData& a = arcs[0]->as_arc_dimension();
    eq(static_cast<int>(a.dimension.dim_type), 5, "the type nibble of 70 (37)");
    eq(a.dimension.flags & 15, 5, "70");
    near(a.dimension.def_point4, {150, 110, 0}, "15: the center");
    near(a.dimension.def_point2, {170, 110, 0}, "13");
    near(a.dimension.def_point3, {150, 130, 0}, "14");
    near(a.end_parameter, 1.570796326794896, "41");
    assert_false(a.has_leader, "71");
    assert_true(!a.proxy_graphics.empty(), "the entity's own proxy graphics");
    std::vector<const Entity*> larges = entities_of(d, EntityType::LargeRadialDimension);
    eq(larges.size(), static_cast<size_t>(2), "two LARGE_RADIAL_DIMENSIONs");
    const LargeRadialDimensionData& l = larges[0]->as_large_radial_dimension();
    eq(l.dimension.flags & 15, 9, "the type nibble 9 (169)");
    assert_true((l.dimension.flags & 128) != 0, "the text is where the user put it");
    near(l.center_override, {10, 90, 0}, "13");
    near(l.jog_point, {11.07965908853561, 95.48031881398094, 0}, "14: half way", 1e-9);
    near(l.dimension.def_point4, {12.15931817707122, 100.9606376279618, 0}, "15");
    near(l.dimension.definition_point, {30, 110, 0}, "10: the circle's center", 1e-9);
}

namespace {

// The record of a special dimension written from its typed values against AutoCAD's: group 42 (the measurement) is computed by
// HDxf from the points as written, 16 digits, where AutoCAD measures its own unrounded geometry (20.00000000000001 against
// 20.00000000000005 for a radius of 20): the two are one number to 1e-12, and the text of AutoCAD's is taken then. Every other group
// must be the same text
std::string typed_difference(const std::string& expected, const std::string& got) {
    auto lines = [](const std::string& text) {
        std::vector<std::string> out;
        size_t at = 0;
        while (at < text.size()) {
            size_t end = text.find('\n', at);
            if (end == std::string::npos) end = text.size();
            out.push_back(text.substr(at, end - at));
            at = end + 1;
        }
        return out;
    };
    std::vector<std::string> a = lines(expected), b = lines(got);
    for (size_t i = 1; i < a.size() && i < b.size(); i += 2) {
        if (a[i - 1] != "42" && a[i - 1] != " 42") continue;
        if (a[i - 1] != b[i - 1] || a[i] == b[i]) continue;
        char* end_a = nullptr;
        char* end_b = nullptr;
        const double x = std::strtod(a[i].c_str(), &end_a);
        const double y = std::strtod(b[i].c_str(), &end_b);
        if (*end_a == '\0' && *end_b == '\0' && close(x, y, 1e-12)) b[i] = a[i];
    }
    std::string joined;
    for (const std::string& line : b) joined += line + "\n";
    return assoc_test::record_difference(expected, joined);
}

} // namespace

TEST(special_dimensions_written_unchanged_and_from_typed_values_is_autocads_record) {
    int compared = 0;
    for (const char* theme : {"dim_special", "dimensions", "annotative"}) {
        for (const char* tag : kTags) {
            for (bool binary : {false, true}) {
                if (!std::filesystem::exists(facit(tag, theme, binary))) continue;
                std::vector<uint8_t> original = assoc_test::as_ascii(facit_bytes(tag, theme, binary));
                Drawing d = load_facit(tag, theme, binary);
                std::vector<uint8_t> unchanged = d.save_to_memory(DxfFormat::Ascii);
                assert_true(!unchanged.empty(), d.last_error());
                std::vector<std::pair<std::string, std::string>> ids;
                for (const Entity* e : all_entities(d)) {
                    if (e->type == EntityType::ArcDimension) ids.push_back({"ARC_DIMENSION", e->props.handle});
                    if (e->type == EntityType::LargeRadialDimension) ids.push_back({"LARGE_RADIAL_DIMENSION", e->props.handle});
                }
                for (const auto& id : ids) {
                    std::string a = assoc_test::ascii_record(original, id.first, id.second);
                    std::string b = assoc_test::ascii_record(unchanged, id.first, id.second);
                    eq(assoc_test::record_difference(a, b), "", std::string(theme) + " " + tag + " unchanged " + id.first + " " + id.second);
                }
                for (Entity& e : d.entities()) {
                    if (e.type == EntityType::ArcDimension || e.type == EntityType::LargeRadialDimension) e.raw = RawRecord();
                }
                // A binary file is compared as text through the same reading and writing (as the other facit tests do): its numbers have no width there
                std::vector<uint8_t> typed = binary ? assoc_test::as_ascii(d.save_to_memory(DxfFormat::Binary)) : d.save_to_memory(DxfFormat::Ascii);
                assert_true(!typed.empty(), d.last_error());
                for (const auto& id : ids) {
                    std::string a = assoc_test::ascii_record(original, id.first, id.second);
                    std::string b = assoc_test::ascii_record(typed, id.first, id.second);
                    if (a.empty()) continue;            // a dimension in a block has another owner chain
                    eq(typed_difference(a, b), "", std::string(theme) + " " + tag + " typed " + id.first + " " + id.second);
                    ++compared;
                }
            }
        }
    }
    if (compared == 0) hdxf_test::skip("no dimension facit");
}

namespace {

// AutoCAD's block of the dimension and the one HDxf builds when the dimension has none, entity for entity
std::string block_difference(const Block& acad, const Block& built) {
    std::vector<const Entity*> a, b;
    for (const Entity& e : acad.entities) a.push_back(&e);
    for (const Entity& e : built.entities) b.push_back(&e);
    if (a.size() != b.size()) return "AutoCAD's block has " + std::to_string(a.size()) + " entities, HDxf's " + std::to_string(b.size());
    const double eps = 1e-6;
    for (size_t i = 0; i < a.size(); ++i) {
        const std::string at = "entity " + std::to_string(i) + " (" + std::to_string(static_cast<int>(a[i]->type)) + "): ";
        if (a[i]->type != b[i]->type) return at + "another type";
        auto p = [&](const Point3& x, const Point3& y) { return close(x.x, y.x, eps) && close(x.y, y.y, eps) && close(x.z, y.z, eps); };
        switch (a[i]->type) {
            case EntityType::Line: {
                const LineData& x = a[i]->as_line();
                const LineData& y = b[i]->as_line();
                if (!p(x.start, y.start) || !p(x.end, y.end)) return at + "a line's ends";
                break;
            }
            case EntityType::Arc: {
                const ArcData& x = a[i]->as_arc();
                const ArcData& y = b[i]->as_arc();
                if (!p(x.center, y.center) || !close(x.radius, y.radius, eps)) {
                    auto show = [](const ArcData& v) {
                        return "(" + std::to_string(v.center.x) + " " + std::to_string(v.center.y) + " " + std::to_string(v.center.z) + ") r " +
                               std::to_string(v.radius);
                    };
                    return at + "an arc's center or radius: " + show(x) + " against " + show(y);
                }
                // An angle is the same one a turn apart: AutoCAD writes its symbol's start as 359.9999999999961 in one dimension and as
                // 0 in the next (its own rounding of 0), which is the same arc
                auto same_angle = [](double u, double v) {
                    double turn = std::fmod(u - v, 360.0);
                    if (turn < 0.0) turn += 360.0;
                    return turn <= 1e-6 || 360.0 - turn <= 1e-6;
                };
                if (!same_angle(x.start_angle, y.start_angle) || !same_angle(x.end_angle, y.end_angle)) {
                    return at + "an arc's angles " + std::to_string(x.start_angle) + " " + std::to_string(x.end_angle) + " against " +
                           std::to_string(y.start_angle) + " " + std::to_string(y.end_angle);
                }
                break;
            }
            case EntityType::Solid: {
                const SolidData& x = a[i]->as_solid();
                const SolidData& y = b[i]->as_solid();
                if (!p(x.p0, y.p0) || !p(x.p1, y.p1) || !p(x.p2, y.p2) || !p(x.p3, y.p3)) {
                    auto show = [](const SolidData& v) {
                        std::string out;
                        for (const Point3* q : {&v.p0, &v.p1, &v.p2, &v.p3}) out += "(" + std::to_string(q->x) + " " + std::to_string(q->y) + ") ";
                        return out;
                    };
                    return at + "an arrowhead: " + show(x) + "against " + show(y);
                }
                break;
            }
            case EntityType::MText: {
                const MTextData& x = a[i]->as_mtext();
                const MTextData& y = b[i]->as_mtext();
                if (!p(x.insertion_point, y.insertion_point)) return at + "the text's place";
                if (x.content != y.content) return at + "the text: \"" + x.content + "\" against \"" + y.content + "\"";
                break;
            }
            case EntityType::Point: {
                if (!p(a[i]->as_point().location, b[i]->as_point().location)) return at + "a definition point";
                break;
            }
            default:
                break;
        }
    }
    return {};
}

} // namespace

// The block of every special dimension of AutoCAD's files, built again by HDxf from the dimension alone: the extension lines, the
// dimension arc broken around the text, the arrowheads, the text with the arc symbol before it, the points; the jog of a large
// radius with its two parallel lines
TEST(special_dimension_blocks_are_built_as_autocad_builds_them) {
    AutoCadFonts fonts;
    int checked = 0;
    for (const char* theme : {"dim_special", "dimensions"}) {
        for (const char* tag : {"2010", "2018"}) {
            if (!std::filesystem::exists(facit(tag, theme, false))) continue;
            Drawing d = load_facit(tag, theme, false);
            // The AutoCAD blocks by name, then the dimensions without them
            std::map<std::string, Block> acad;
            std::vector<std::string> names;
            for (Entity& e : d.entities()) {
                if (e.type != EntityType::ArcDimension && e.type != EntityType::LargeRadialDimension) continue;
                DimensionData& dim = *e.dimension_part();
                const Block* b = d.find_block(dim.block_name);
                if (!b) continue;
                acad[e.props.handle] = *b;
                names.push_back(e.props.handle);
                dim.block_name.clear();
                e.raw = RawRecord();
            }
            if (names.empty()) continue;
            Drawing r = reload(d);
            for (const Entity* e : all_entities(r)) {
                if (e->type != EntityType::ArcDimension && e->type != EntityType::LargeRadialDimension) continue;
                auto it = acad.find(e->props.handle);
                if (it == acad.end()) continue;
                const Block* built = r.find_block(e->dimension_part()->block_name);
                assert_true(built != nullptr, "the block HDxf built for " + e->props.handle);
                // Only the dimensions AutoCAD gave default text and symbol (the files have DIMARCSYM 0 and DIMTOH 1)
                std::string why = block_difference(it->second, *built);
                eq(why, "", std::string(theme) + " " + tag + " " + (e->type == EntityType::ArcDimension ? "ARC_DIMENSION " : "LARGE_RADIAL_DIMENSION ") + e->props.handle);
                ++checked;
            }
        }
    }
    if (checked == 0) hdxf_test::skip("no special dimension facit");
}

// The extents of a special dimension are its block's and its definition point (as a DIMENSION's): AutoCAD's block of the first
// ARC_DIMENSION reaches x 181.8 (the extension line past the dimension arc)
TEST(special_dimensions_count_in_the_extents) {
    Drawing d = load_facit("2018", "dim_special");
    std::vector<const Entity*> arcs = entities_of(d, EntityType::ArcDimension);
    assert_true(!arcs.empty(), "an ARC_DIMENSION");
    Point3 lo, hi;
    assert_true(d.entity_extents(*arcs[0], lo, hi), "extents of the ARC_DIMENSION");
    assert_true(hi.x >= 181.8 - 1e-3 && lo.x <= 150.0 + 1e-9, "its block reaches the extension line past the arc");
    std::vector<const Entity*> larges = entities_of(d, EntityType::LargeRadialDimension);
    assert_true(!larges.empty(), "a LARGE_RADIAL_DIMENSION");
    assert_true(d.entity_extents(*larges[0], lo, hi), "extents of the LARGE_RADIAL_DIMENSION");
    assert_true(lo.x <= 5.2 && hi.x >= 30.0 - 1e-9, "from the jog's far end to the center (10)");
}

// A new ARC_DIMENSION and a new LARGE_RADIAL_DIMENSION in every version: the block built, read back, written unchanged, in the
// other format; before 2000 the named error
TEST(special_dimension_version_matrix) {
    AutoCadFonts fonts;
    for (const char* name : {"ARC_DIMENSION", "LARGE_RADIAL_DIMENSION"}) {
        const internal::EntityCodec* codec = internal::TypedRegistry::instance().find_entity(name);
        assert_true(codec != nullptr, name);
        const bool arc = std::string(name) == "ARC_DIMENSION";
        auto add = [&](Drawing& d) {
            if (arc) {
                d.add_arc_dimension({150, 110, 0}, {170, 110, 0}, {150, 130, 0}, {177.386, 125.811, 0});
            } else {
                d.add_large_radial_dimension({30, 110, 0}, {12.159, 100.96, 0}, {10, 90, 0}, {10.77, 100.26, 0});
            }
        };
        auto verify = [&](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            std::vector<const Entity*> found = entities_of(r, arc ? EntityType::ArcDimension : EntityType::LargeRadialDimension);
            if (found.size() != 1) return std::string(name) + ": " + std::to_string(found.size()) + " records";
            const DimensionData& dim = *found[0]->dimension_part();
            const Block* b = r.find_block(dim.block_name);
            if (!b || b->entities.empty()) return "no block of its own";
            if (arc) {
                if (!close(found[0]->as_arc_dimension().dimension.def_point4.x, 150.0, 1e-9)) return "15";
            } else if (!close(found[0]->as_large_radial_dimension().center_override.x, 10.0, 1e-9)) {
                return "13";
            }
            return {};
        };
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix(name, codec->versions, add, verify);
        assert_true(report.generated > 0, std::string(name) + ": nothing generated");
        eq(report.named_errors, 6, std::string(name) + ": R12, R13 and R14 in both formats");
    }
}

// A special dimension read from a file and changed: its block is built again (B12), the others stay as AutoCAD's
TEST(special_dimension_changed_in_a_file_gets_a_new_block) {
    AutoCadFonts fonts;
    Drawing d = load_facit("2018", "dim_special");
    Entity* arc = nullptr;
    for (Entity& e : d.entities()) if (e.type == EntityType::ArcDimension) { arc = &e; break; }
    assert_true(arc != nullptr, "an ARC_DIMENSION");
    const std::string handle = arc->props.handle;
    const std::string block = arc->as_arc_dimension().dimension.block_name;
    const Block old_block = *d.find_block(block);
    arc->as_arc_dimension().dimension.def_point4 = {150, 110, 0};      // the same: nothing changes
    {
        Drawing same = reload(d);
        const Entity* e = same.find_entity(handle);
        near(same.find_block(e->as_arc_dimension().dimension.block_name)->entities.size() * 1.0,
             old_block.entities.size() * 1.0, "the block of an unchanged dimension");
    }
    arc->as_arc_dimension().dimension.def_point2 = {180, 110, 0};      // the first end moved out
    Drawing r = reload(d);
    const Entity* e = r.find_entity(handle);
    assert_true(e != nullptr, "the dimension");
    near(e->as_arc_dimension().dimension.def_point2, {180, 110, 0}, "13");
    const Block* nb = r.find_block(e->as_arc_dimension().dimension.block_name);
    assert_true(nb != nullptr, "its block");
    bool moved = false;
    for (const Entity& x : nb->entities) {
        if (x.type == EntityType::Point && close(x.as_point().location.x, 180.0, 1e-9)) moved = true;
    }
    assert_true(moved, "the block has the new definition point");
}

// ---------------------------------------------------------------------------
// 9.S (Codex 7, 9, 10, 17)
// ---------------------------------------------------------------------------

namespace {

Point3 header_point(const Drawing& d, const char* name) {
    Point3 p{};
    const HeaderVar* v = d.find_header(name);
    if (!v) throw hdxf_test::AssertFailure{std::string("no ") + name};
    for (const auto& [code, value] : v->values) {
        if (code == 10) p.x = std::stod(value);
        if (code == 20) p.y = std::stod(value);
        if (code == 30) p.z = std::stod(value);
    }
    return p;
}

// The model space extents HDxf counts for a drawing (extents.cpp), as the writer takes them for a drawing built in memory
void model_extents(const Drawing& d, Point3& lo, Point3& hi) {
    internal::Extents model, paper;
    std::vector<std::string> warnings;
    internal::drawing_extents(d, {}, {}, model, paper, warnings);
    lo = model.min;
    hi = model.max;
}

} // namespace

// Codex 17 (plan 9.17): an annotative TEXT, MTEXT, dimension, LEADER, MULTILEADER, TOLERANCE, INSERT or hatch counts in
// $EXTMIN/$EXTMAX as it is drawn at the current annotation scale (CANNOSCALE, the DICTIONARYVAR) — by its context there; in 2000 …
// 2007 by the copy of an annotative object's decomposition at that scale. AutoCAD 2025's own extents, every version and both
// formats: fas9_cannoscale (the same drawing at 1:1, 1:2 and 1:5 after REGENALL: every side), fas9_mtext_width annotative (an MTEXT
// at 1:5: its context's 42 × 43, not its width), contexts rotated and leader_stretched (1:1). The sides that rules outside the
// representation decide are not compared, each named: contexts autoscale, scale_removed and scale_list_deleted (1:10) — the right
// and top are a TEXT turned 30 degrees, whose extents AutoCAD takes from its glyphs and HDxf from its box (R.8; 468.32/303.48 against
// 465.82/292.32); Erik's annotative and annotative scale_removed (1:2) — the top is an MTEXT attribute in Arial, whose line AutoCAD
// counts 0.2647 of its height above its top (the TrueType MTEXT box of R.8, 227.10 against 228.588; plain: fas9_mtext_width), and
// annotative's left is a circle the script moved into the block (-5: AutoCAD's header kept it, scale_removed after another
// REGENALL has -0.013). fas9_contexts itself is saved before a REGENALL: its header is not the drawing's (cannoscale is the same
// drawing regenerated, and HDxf's extents are its)
TEST(annotation_scale_extents_are_autocads) {
    AutoCadFonts fonts;
    enum : int { Left = 1, Bottom = 2, Right = 4, Top = 8, All = 15 };
    const std::vector<std::pair<const char*, int>> themes = {
        {"cannoscale", All}, {"cannoscale_1_2", All}, {"cannoscale_1_5", All}, {"mtext_width_annotative", All},
        {"contexts_rotated", All}, {"contexts_leader_stretched", All},
        {"contexts_autoscale", Left | Bottom}, {"contexts_scale_removed", Left | Bottom}, {"contexts_scale_list_deleted", Left | Bottom},
        {"annotative", Bottom | Right}, {"annotative_scale_removed", Bottom | Right}};
    int checked = 0;
    std::string report;
    for (const auto& [theme, sides] : themes) {
        for (const char* tag : kTags) {
            for (bool binary : {false, true}) {
                std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(facit(tag, theme, binary));
                if (bytes.empty()) continue;
                Drawing d;
                assert_true(d.load_from_memory(bytes.data(), bytes.size()), d.last_error());
                Point3 lo, hi;
                model_extents(d, lo, hi);
                const Point3 alo = header_point(d, "$EXTMIN"), ahi = header_point(d, "$EXTMAX");
                // AutoCAD's own numbers carry its rounding (-22.6680106 for -22.6680103): 1e-6
                bool same = true;
                if (sides & Left) same = same && std::fabs(lo.x - alo.x) <= 1e-6;
                if (sides & Bottom) same = same && std::fabs(lo.y - alo.y) <= 1e-6;
                if (sides & Right) same = same && std::fabs(hi.x - ahi.x) <= 1e-6;
                if (sides & Top) same = same && std::fabs(hi.y - ahi.y) <= 1e-6;
                if (!same) {
                    char line[400];
                    std::snprintf(line, sizeof(line), "%s %s%s: HDxf %.9g %.9g .. %.9g %.9g, AutoCAD %.9g %.9g .. %.9g %.9g\n",
                                  theme, tag, binary ? " bin" : "", lo.x, lo.y, hi.x, hi.y, alo.x, alo.y, ahi.x, ahi.y);
                    report += line;
                }
                ++checked;
            }
        }
    }
    if (checked == 0) hdxf_test::skip("no annotative facit");
    eq(report, "", "extents at the current annotation scale");
}

namespace {

Entity* entity_in(Drawing& d, const std::string& handle) {
    for (Entity& e : d.entities()) if (e.props.handle == handle) return &e;
    for (Block& b : d.blocks()) for (Entity& e : b.entities) if (e.props.handle == handle) return &e;
    return nullptr;
}

const Entity* entity_in(const Drawing& d, const std::string& handle) {
    return entity_in(const_cast<Drawing&>(d), handle);
}

std::vector<std::string> handles_of(const Block& b) {
    std::vector<std::string> out;
    for (const Entity& e : b.entities) out.push_back(e.props.handle);
    return out;
}

bool has_point_at_x(const Block& b, double x) {
    for (const Entity& e : b.entities) {
        if (e.type == EntityType::Point && close(e.as_point().location.x, x, 1e-9)) return true;
    }
    return false;
}

// The group codes of a record's AcDbDimension subclass in their order
std::vector<int> dimension_codes(const std::string& record) {
    std::vector<int> out;
    std::vector<std::string> lines;
    size_t at = 0;
    while (at < record.size()) {
        size_t end = record.find('\n', at);
        if (end == std::string::npos) end = record.size();
        lines.push_back(record.substr(at, end - at));
        at = end + 1;
    }
    bool in = false;
    for (size_t i = 0; i + 1 < lines.size(); i += 2) {
        const int code = std::atoi(lines[i].c_str());
        if (code == 100) {
            in = lines[i + 1] == "AcDbDimension";
            continue;
        }
        if (in) out.push_back(code);
    }
    return out;
}

} // namespace

// Codex 7: an ARC_DIMENSION inside a block (AutoCAD's dim_special_block: HDXF_SPECIAL holds DIMARC and DIMJOGGED) that changes gets
// its *D block anew, as one in model space does; the LARGE_RADIAL_DIMENSION beside it that did not change keeps AutoCAD's
TEST(special_dimension_changed_in_a_block_gets_a_new_block) {
    AutoCadFonts fonts;
    Drawing d = load_facit("2018", "dim_special_block");
    Entity* arc = entity_in(d, "2720");
    Entity* large = entity_in(d, "2723");
    assert_true(arc && arc->type == EntityType::ArcDimension, "the ARC_DIMENSION in HDXF_SPECIAL");
    assert_true(large && large->type == EntityType::LargeRadialDimension, "the LARGE_RADIAL_DIMENSION in HDXF_SPECIAL");
    const std::string arc_block = arc->as_arc_dimension().dimension.block_name;
    const std::string large_block = large->as_large_radial_dimension().dimension.block_name;
    const std::vector<std::string> arc_before = handles_of(*d.find_block(arc_block));
    const std::vector<std::string> large_before = handles_of(*d.find_block(large_block));
    Point3 moved = arc->as_arc_dimension().dimension.def_point2;
    const Point3 center = arc->as_arc_dimension().dimension.def_point4;
    moved = {center.x + (moved.x - center.x) * 1.5, center.y + (moved.y - center.y) * 1.5, moved.z};
    arc->as_arc_dimension().dimension.def_point2 = moved;
    std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!bytes.empty(), d.last_error());
    bool warned = false;
    for (const std::string& w : d.warnings()) warned = warned || w.find("ARC_DIMENSION 2720 changed") != std::string::npos;
    assert_true(warned, "the warning names the dimension whose proxy graphics are not written");
    Drawing r = assoc_test::reload(bytes);
    const Block* nb = r.find_block(arc_block);
    assert_true(nb != nullptr && !nb->entities.empty(), "the ARC_DIMENSION's block");
    for (const std::string& h : handles_of(*nb)) {
        assert_true(std::find(arc_before.begin(), arc_before.end(), h) == arc_before.end(), "new geometry, new handles: " + h);
    }
    assert_true(has_point_at_x(*nb, moved.x), "the block has the new definition point");
    eq(handles_of(*r.find_block(large_block)).size(), large_before.size(), "the unchanged LARGE_RADIAL_DIMENSION's block");
    eq(handles_of(*r.find_block(large_block))[0], large_before[0], "the unchanged LARGE_RADIAL_DIMENSION's block keeps AutoCAD's");
    // Codex 9: its proxy graphics showed the arc before the change: written without (as a new one); the unchanged one keeps them
    const Entity* back = entity_in(r, "2720");
    assert_true(back != nullptr, "the changed ARC_DIMENSION");
    assert_true(back->as_arc_dimension().proxy_graphics.empty(), "no proxy graphics of the dimension as it was");
    const Entity* kept = entity_in(r, "2723");
    assert_true(kept != nullptr, "the unchanged LARGE_RADIAL_DIMENSION");
    assert_false(kept->as_large_radial_dimension().proxy_graphics.empty(), "AutoCAD's proxy graphics kept");
}

/// Codex 7: a dimension whose preserved record was emptied is not in the drawing as read (that is made of the preserved records), so
// a change cannot be told from its record: it is written from its values, as a dimension built in memory is, and keeps the block it
// names — unchanged it keeps AutoCAD's block and proxy graphics. Drawn anew when the program clears its block name: a block is built
TEST(special_dimension_with_its_record_emptied_keeps_its_block_unless_its_name_is_cleared) {
    AutoCadFonts fonts;
    Drawing d = load_facit("2018", "dim_special");
    Entity* arc = entity_in(d, "306");
    assert_true(arc && arc->type == EntityType::ArcDimension, "ARC_DIMENSION 306");
    const std::string block = arc->as_arc_dimension().dimension.block_name;
    const std::vector<std::string> before = handles_of(*d.find_block(block));
    arc->raw = RawRecord();
    {
        Drawing same = reload(d);
        eq(handles_of(*same.find_block(block))[0], before[0], "an emptied record keeps AutoCAD's block");
        assert_false(entity_in(same, "306")->as_arc_dimension().proxy_graphics.empty(), "and its proxy graphics");
    }
    arc->as_arc_dimension().dimension.def_point2 = {180, 110, 0};
    arc->as_arc_dimension().dimension.block_name.clear();
    Drawing r = reload(d);
    const Entity* back = entity_in(r, "306");
    assert_true(back != nullptr, "the dimension");
    const Block* nb = r.find_block(back->as_arc_dimension().dimension.block_name);
    assert_true(nb != nullptr && has_point_at_x(*nb, 180.0), "its block name cleared: a block built from its values");
}

// Codex 7: the same for a DIMENSION in a block: made, written, read (a drawing read from a file), its point moved — its *D block is
// built anew with new handles
TEST(dimension_changed_in_a_block_gets_a_new_block) {
    AutoCadFonts fonts;
    Drawing made(DxfVersion::R2018);
    made.add_block("HDXF_DIMS");
    Entity dim = made.add_dimension(DimensionType::Aligned, {40, 20, 0}, {20, 20, 0});
    made.entities().pop_back();
    dim.as_dimension().def_point2 = {0, 0, 0};
    dim.as_dimension().def_point3 = {40, 0, 0};
    made.find_block("HDXF_DIMS")->entities.push_back(dim);
    made.add_insert("HDXF_DIMS", {100, 100, 0});
    Drawing d = reload(made);
    Entity* inside = nullptr;
    for (Entity& e : d.find_block("HDXF_DIMS")->entities) if (e.type == EntityType::Dimension) inside = &e;
    assert_true(inside != nullptr, "the DIMENSION in the block");
    const std::string block = inside->as_dimension().block_name;
    assert_true(d.find_block(block) != nullptr, "its *D block, read");
    const std::vector<std::string> before = handles_of(*d.find_block(block));
    {
        Drawing same = reload(d);
        eq(handles_of(*same.find_block(block))[0], before[0], "unchanged: the block as read");
    }
    inside->as_dimension().def_point3 = {60, 0, 0};
    inside->as_dimension().definition_point = {60, 20, 0};
    Drawing r = reload(d);
    const Block* nb = r.find_block(block);
    assert_true(nb != nullptr && !nb->entities.empty(), "the block");
    for (const std::string& h : handles_of(*nb)) {
        assert_true(std::find(before.begin(), before.end(), h) == before.end(), "new geometry, new handles: " + h);
    }
    assert_true(has_point_at_x(*nb, 60.0), "the block has the moved definition point");
}

// Codex 9: the extents count a regenerated block, not the one the drawing holds (drawing_extents' `replaced`, given by the writer
// before it takes the extents)
TEST(extents_count_a_regenerated_dimension_block) {
    Drawing d = load_facit("2018", "dim_special");
    const Entity* arc = d.find_entity("306");
    assert_true(arc != nullptr, "ARC_DIMENSION 306");
    Block replaced;
    replaced.name = arc->as_arc_dimension().dimension.block_name;
    replaced.base_point = d.find_block(replaced.name)->base_point;
    Entity far;
    far.type = EntityType::Point;
    PointData p;
    p.location = {5000, 6000, 0};
    far.data = p;
    replaced.entities.push_back(far);
    internal::Extents model, paper;
    std::vector<std::string> warnings;
    std::vector<Block> list{replaced};
    internal::drawing_extents(d, {}, {}, model, paper, warnings, &list);
    near(model.max.x, 5000.0, "the regenerated block's point counts");
    near(model.max.y, 6000.0, "the regenerated block's point counts");
}

// Codex 10: AcDbDimension's 71 and 73 … 75 are read into DimensionData, a special dimension's 72 and 41 into its own fields, and
// written from there (AutoCAD's files: 71 5,
// 73 74 75 0, no 72 and 41); 1 stands between 70 and 71 as in AutoCAD's files; an ARC_DIMENSION's 12 is in AcDbDimension
TEST(dimension_text_groups_are_read_and_written_from_the_typed_values) {
    int dims = 0;
    for (const char* theme : {"dim_special", "dimensions"}) {
        for (const char* tag : kTags) {
            if (!std::filesystem::exists(facit(tag, theme, false))) continue;
            Drawing d = load_facit(tag, theme);
            for (const Entity* e : all_entities(d)) {
                const DimensionData* dim = e->dimension_part();
                if (!dim) continue;
                eq(dim->attachment_point, 5, std::string(theme) + " " + tag + " 71 of " + e->props.handle);
                if (e->type == EntityType::ArcDimension) {
                    assert_false(e->as_arc_dimension().line_spacing_style || e->as_arc_dimension().line_spacing_factor, "no 72 or 41");
                } else if (e->type == EntityType::LargeRadialDimension) {
                    assert_false(e->as_large_radial_dimension().line_spacing_style || e->as_large_radial_dimension().line_spacing_factor,
                                 "no 72 or 41");
                }
                eq(dim->value_73 + dim->value_74 + dim->value_75, 0, "73 74 75");
                ++dims;
            }
        }
    }
    if (dims == 0) hdxf_test::skip("no dimension facit");

    for (DxfVersion v : {DxfVersion::R2010, DxfVersion::R2018}) {
        Drawing made(v);
        Entity& arc = made.add_arc_dimension({150, 110, 0}, {170, 110, 0}, {150, 130, 0}, {177.386, 125.811, 0});
        DimensionData& a = arc.as_arc_dimension().dimension;
        a.attachment_point = 7;
        arc.as_arc_dimension().line_spacing_style = 2;
        arc.as_arc_dimension().line_spacing_factor = 1.5;
        a.value_73 = 1;
        a.value_74 = 2;
        a.value_75 = 3;
        a.text_override = "<> arc";
        a.block_translation = {1, 2, 0};
        std::vector<uint8_t> bytes = made.save_to_memory(DxfFormat::Ascii);
        assert_true(!bytes.empty(), made.last_error());
        Drawing r = assoc_test::reload(bytes);
        const Entity* back = nullptr;
        for (const Entity& e : r.entities()) if (e.type == EntityType::ArcDimension) back = &e;
        assert_true(back != nullptr, "the ARC_DIMENSION");
        const DimensionData& b = back->as_arc_dimension().dimension;
        eq(b.attachment_point, 7, "71");
        assert_true(back->as_arc_dimension().line_spacing_style && *back->as_arc_dimension().line_spacing_style == 2, "72");
        assert_true(back->as_arc_dimension().line_spacing_factor && close(*back->as_arc_dimension().line_spacing_factor, 1.5, 1e-12), "41");
        eq(b.value_73 * 100 + b.value_74 * 10 + b.value_75, 123, "73 74 75");
        near(b.block_translation, {1, 2, 0}, "12 of an ARC_DIMENSION");
        std::vector<int> codes = dimension_codes(assoc_test::ascii_record(bytes, "ARC_DIMENSION", back->props.handle));
        std::vector<int> expected = {280, 2, 10, 20, 30, 11, 21, 31, 12, 22, 32, 70, 1, 71, 72, 41, 42, 73, 74, 75, 3};
        std::string got;
        for (int c : codes) got += std::to_string(c) + " ";
        std::string want;
        for (int c : expected) want += std::to_string(c) + " ";
        eq(got, want, "AcDbDimension in AutoCAD's order (2010+)");
    }
}
