#include "test_framework.hpp"
#include "type_version_matrix.hpp"
#include "typed_corpus.hpp"
#include "hdxf.hpp"
#include "TypedRecordTypes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Step 9.2 of phase 9 (plan 2026-10-27-fas-9-typer.md): WIPEOUT, WIPEOUTVARIABLES and SCALE (codecs for what
// HDxf typed before phase 9), the colors of BLOCK and ENDBLK, and the vertex identifiers (91) of LWPOLYLINE and
// POLYLINE/VERTEX (F9.21).
//
// The facit is what AutoCAD 2025's core saved from doc/facit/fas9_existing_fields.scr (acad_<version>_dxf_wipeout*,
// scales*, blockcolors and vertexids*, R12 and 2000 … 2018, ASCII and binary; the files are not in git, a test
// that needs one is skipped without it) and the files of the older facit. R13 and R14 AutoCAD 2025 cannot save:
// the AutoCAD R14 file 7H12R10F_schneider_r14_dimension_spline.dxf of build/test-data/local-r13r14 is the one
// observation of an ENDBLK without the color of its BLOCK.
// ---------------------------------------------------------------------------

using hdxf::Color;
using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::ImageData;
using hdxf::ObjectType;
using hdxf::Point2;
using hdxf::Point3;
using namespace hdxf::internal;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

namespace {

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

const char* const kAllTags[7] = {"r12", "2000", "2004", "2007", "2010", "2013", "2018"};
const char* const kTypedTags[6] = {"2000", "2004", "2007", "2010", "2013", "2018"};

DxfVersion version_of(const std::string& tag) {
    if (tag == "r12") return DxfVersion::R12;
    if (tag == "2000") return DxfVersion::R2000;
    if (tag == "2004") return DxfVersion::R2004;
    if (tag == "2007") return DxfVersion::R2007;
    if (tag == "2010") return DxfVersion::R2010;
    if (tag == "2013") return DxfVersion::R2013;
    return DxfVersion::R2018;
}

std::string facit_name(const std::string& tag, const std::string& theme, bool binary) {
    return "acad_" + tag + "_dxf_" + theme + (binary ? "_bin" : "") + ".dxf";
}

// The bytes of an AutoCAD file of doc/facit; the test is skipped when it is not there
std::vector<uint8_t> facit(const std::string& name) {
    std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(hdxf_test::source_path("doc/facit/" + name));
    if (bytes.empty()) hdxf_test::skip("missing doc/facit/" + name + " (made by doc/facit/fas9_existing_fields.scr)");
    return bytes;
}

DxfFormat format_for(bool binary) { return binary ? DxfFormat::Binary : DxfFormat::Ascii; }

void load(hdxf::Drawing& d, const std::vector<uint8_t>& bytes, const std::string& what) {
    assert_true(d.load_from_memory(bytes.data(), bytes.size()), what + ": " + d.last_error());
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
    size_t pos = 0;
    while (pos < a.size() && pos < b.size() && a[pos] == b[pos]) ++pos;
    size_t line = 1;
    for (size_t i = 0; i < pos; ++i) if (a[i] == '\n') ++line;
    return "first difference at byte " + std::to_string(pos) + " (line " + std::to_string(line) + "), " +
           std::to_string(a.size()) + " and " + std::to_string(b.size()) + " bytes";
}

// ---------------------------------------------------------------------------
// A small reader of ASCII DXF text, for what the writer wrote
// ---------------------------------------------------------------------------

struct Group {
    int code;
    std::string value;
};
using Record = std::vector<Group>;

std::string trimmed(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

std::vector<Record> ascii_records(const std::vector<uint8_t>& bytes) {
    std::string text(bytes.begin(), bytes.end());
    std::vector<Record> records;
    std::istringstream in(text);
    std::string code_line, value_line;
    while (std::getline(in, code_line) && std::getline(in, value_line)) {
        int code = std::atoi(trimmed(code_line).c_str());
        if (!value_line.empty() && value_line.back() == '\r') value_line.pop_back();
        if (code == 0) records.emplace_back();
        if (records.empty()) continue;
        records.back().push_back({code, code == 0 || code == 2 || code == 8 || code == 300 ? trimmed(value_line) : value_line});
    }
    return records;
}

std::string type_of(const Record& r) { return r.empty() ? std::string() : r[0].value; }

std::optional<std::string> value_of(const Record& r, int code, size_t ordinal = 0) {
    for (const Group& g : r) {
        if (g.code == code && ordinal-- == 0) return trimmed(g.value);
    }
    return std::nullopt;
}

int index_of(const Record& r, int code) {
    for (size_t i = 0; i < r.size(); ++i) if (r[i].code == code) return static_cast<int>(i);
    return -1;
}

// The BLOCK records and the ENDBLK that closes each, by block name
struct BlockPair {
    Record begin;
    Record end;
};

std::vector<BlockPair> block_pairs(const std::vector<Record>& records) {
    std::vector<BlockPair> out;
    for (const Record& r : records) {
        if (type_of(r) == "BLOCK") out.push_back({r, {}});
        else if (type_of(r) == "ENDBLK" && !out.empty()) out.back().end = r;
    }
    return out;
}

const BlockPair* find_pair(const std::vector<BlockPair>& pairs, const std::string& name) {
    for (const BlockPair& p : pairs) {
        auto n = value_of(p.begin, 2);
        if (n && *n == name) return &p;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// The code that was there: the codecs of this step taken out of the registry, so that the reader and the writer
// of 1.8.1 handle the types (the plan's integration contract, point 4)
// ---------------------------------------------------------------------------

class OldCode {
public:
    OldCode() {
        TypedRegistry& registry = TypedRegistry::instance();
        if (const EntityCodec* c = registry.find_entity("WIPEOUT")) {
            wipeout_ = *c;
            registry.remove_entity("WIPEOUT");
        }
        if (const ObjectCodec* c = registry.find_object("WIPEOUTVARIABLES")) {
            variables_ = *c;
            registry.remove_object("WIPEOUTVARIABLES");
        }
        if (const ObjectCodec* c = registry.find_object("SCALE")) {
            scale_ = *c;
            registry.remove_object("SCALE");
        }
    }
    ~OldCode() {
        TypedRegistry& registry = TypedRegistry::instance();
        if (wipeout_) registry.add(*wipeout_);
        if (variables_) registry.add(*variables_);
        if (scale_) registry.add(*scale_);
    }
    OldCode(const OldCode&) = delete;
    OldCode& operator=(const OldCode&) = delete;

private:
    std::optional<EntityCodec> wipeout_;
    std::optional<ObjectCodec> variables_;
    std::optional<ObjectCodec> scale_;
};

using Change = std::function<void(hdxf::Drawing&)>;

// The file read, changed and written — with the codecs (old_code false) or by the code that was there
std::vector<uint8_t> written(const std::vector<uint8_t>& in, DxfFormat format, const Change& change, bool old_code,
                             const std::string& what) {
    std::optional<OldCode> old;
    if (old_code) old.emplace();
    hdxf::Drawing d;
    load(d, in, what);
    if (change) change(d);
    std::vector<uint8_t> out = d.save_to_memory(format);
    assert_true(!out.empty(), what + ": " + d.last_error());
    return without_version_guid(std::move(out));
}

// The same as with the old code: the codecs of this step are in the registry and give the same bytes
void same_as_old_code(const std::vector<uint8_t>& in, DxfFormat format, const Change& change, const std::string& what) {
    std::vector<uint8_t> expected = written(in, format, change, true, what);
    std::vector<uint8_t> actual = written(in, format, change, false, what);
    assert_true(expected == actual, what + ": " + first_difference(expected, actual));
}

std::vector<hdxf::Entity*> entities_of(hdxf::Drawing& d, hdxf::EntityType type) {
    std::vector<hdxf::Entity*> out;
    for (auto& e : d.entities()) if (e.type == type) out.push_back(&e);
    for (auto& b : d.blocks()) {
        for (auto& e : b.entities) if (e.type == type) out.push_back(&e);
    }
    return out;
}

std::vector<hdxf::Object*> objects_of(hdxf::Drawing& d, ObjectType type) {
    std::vector<hdxf::Object*> out;
    for (auto& o : d.objects()) if (o.type == type) out.push_back(&o);
    return out;
}

bool near(double a, double b, double tolerance = 1e-9) { return std::fabs(a - b) <= tolerance; }

bool header_point(const hdxf::Drawing& d, const char* name, double& x, double& y) {
    const hdxf::HeaderVar* h = d.find_header(name);
    if (!h || h->values.size() < 2) return false;
    x = std::atof(h->values[0].second.c_str());
    y = std::atof(h->values[1].second.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// WIPEOUT, WIPEOUTVARIABLES, SCALE: scenarios
// ---------------------------------------------------------------------------

// What a program changes on a WIPEOUT: a scalar, a point, the boundary list (the count follows), the boundary kind,
// the clip switch, the properties, the preserved record emptied, the record removed
void change_wipeout(hdxf::Drawing& d, int scenario) {
    std::vector<hdxf::Entity*> w = entities_of(d, hdxf::EntityType::Wipeout);
    assert_true(!w.empty(), "a WIPEOUT");
    ImageData& a = w[0]->as_wipeout();
    switch (scenario) {
        case 1: a.brightness += 10; a.contrast = 20; a.fade = 5; break;
        case 2: a.insertion_point.x += 3.5; a.u_vector.y = 0.25; break;
        case 3: a.clip_boundary.insert(a.clip_boundary.begin() + 2, Point2{0.2, 0.2}); break;
        case 4: a.clip_boundary.pop_back(); break;
        case 5: a.clip_boundary_type = 1; a.clip_boundary = {{-0.5, -0.5}, {0.5, 0.5}}; break;
        case 6: w[0]->props.color = Color::from_aci(3); w[0]->props.visible = false; break;
        case 7: a.clipping = false; a.display_flags = 3; break;
        case 8: for (hdxf::Entity* e : w) e->raw = hdxf::RawRecord(); break;
        case 9: {
            auto& list = d.entities();
            for (size_t i = 0; i < list.size(); ++i) {
                if (&list[i] == w[0]) { list.erase(list.begin() + static_cast<long>(i)); break; }
            }
            break;
        }
        default: break;
    }
}

void change_variables(hdxf::Drawing& d, int scenario) {
    std::vector<hdxf::Object*> v = objects_of(d, ObjectType::WipeoutVariables);
    assert_true(!v.empty(), "WIPEOUTVARIABLES");
    switch (scenario) {
        case 1: v[0]->as_wipeout_variables().display_frame = 0; break;
        case 2: v[0]->as_wipeout_variables().display_frame = 2; break;
        case 3: v[0]->raw = hdxf::RawRecord(); break;
        default: break;
    }
}

const std::string kLongName = [] {
    std::string s = "Scale \xC3\xA9 \xE2\x88\x85 \xF0\x9F\x98\x80 ";
    for (int i = 0; i < 40; ++i) s += "long name \xC3\xB6 ";
    return s;
}();

void change_scales(hdxf::Drawing& d, int scenario) {
    std::vector<hdxf::Object*> s = objects_of(d, ObjectType::Scale);
    assert_true(s.size() >= 4, "scales");
    switch (scenario) {
        case 1: s[1]->as_scale().paper_units = 2.5; s[1]->as_scale().drawing_units = 11.0; break;
        case 2: s[2]->as_scale().name = kLongName; break;
        case 3: s[3]->as_scale().unit_scale = true; s[3]->as_scale().flags = 4; break;
        case 4: for (hdxf::Object* o : s) o->raw = hdxf::RawRecord(); break;
        case 5: {
            auto& list = d.objects();
            for (size_t i = 0; i < list.size(); ++i) {
                if (&list[i] == s[1]) { list.erase(list.begin() + static_cast<long>(i)); break; }
            }
            break;
        }
        default: break;
    }
}

hdxf::Entity wipeout_entity(const std::vector<Point2>& boundary, Point3 at = {1.0, 2.0, 0.0}) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Wipeout;
    ImageData d;
    d.insertion_point = at;
    d.u_vector = {10.0, 0.0, 0.0};
    d.v_vector = {0.0, 10.0, 0.0};
    d.size = {1.0, 1.0};
    d.image_def_handle = "0";
    d.reactor_handle = "0";
    d.clipping = true;
    d.clip_boundary_type = 2;
    d.clip_boundary = boundary;
    e.data = hdxf::Boxed<ImageData>(d);
    e.props.layer = "0";
    return e;
}

const std::vector<Point2> kBoundary = {{-0.5, 0.5}, {0.5, 0.5}, {0.5, -0.1}, {-0.3, -0.3}, {-0.5, 0.5}};

} // namespace

// ---------------------------------------------------------------------------
// The codecs are in the registry, with what the plan's contract asks of a type
// ---------------------------------------------------------------------------

TEST(existing_record_fields_codecs_are_registered_with_a_matrix_and_a_step) {
    TypedRegistry& registry = TypedRegistry::instance();
    for (const char* name : {"WIPEOUT", "WIPEOUTVARIABLES", "SCALE"}) {
        assert_true(registry.implemented(name), std::string(name) + " has a codec");
    }
    const EntityCodec* wipeout = registry.find_entity("WIPEOUT");
    assert_true(wipeout && wipeout->read && wipeout->write && wipeout->extents && wipeout->references, "WIPEOUT's functions");
    assert_eq(2, wipeout->step, "WIPEOUT is step 9.2's");
    assert_eq(2, registry.find_object("WIPEOUTVARIABLES")->step);
    assert_eq(2, registry.find_object("SCALE")->step);
    // 340 and 360 are pointers (0 in AutoCAD's own), the boundary is one list with its count
    const std::vector<RefDescriptor>* refs = registry.refs_for("WIPEOUT");
    assert_true(refs && find_reference_in(refs, "AcDbWipeout", 0, 340, 0) && find_reference_in(refs, "AcDbWipeout", 0, 360, 0),
                "WIPEOUT's pointers");
    bool started = false;
    const std::vector<ListSpec>* lists = registry.lists_for("WIPEOUT");
    for (int code : {91, 14, 24}) {
        assert_eq(1, find_list_in(lists, "AcDbWipeout", 0, code, started), "the boundary list has code " + std::to_string(code));
    }
    assert_eq(0, find_list_in(lists, "AcDbWipeout", 0, 281, started), "brightness is a scalar");
}

// ---------------------------------------------------------------------------
// AutoCAD's WIPEOUT, read and written
// ---------------------------------------------------------------------------

TEST(existing_record_fields_wipeout_of_autocad_is_read_typed_in_every_version_and_format) {
    for (const char* tag : kTypedTags) {
        for (bool binary : {false, true}) {
            const std::string name = facit_name(tag, "wipeout", binary);
            std::vector<uint8_t> in = facit(name);
            hdxf::Drawing d;
            load(d, in, name);
            std::vector<hdxf::Entity*> w = entities_of(d, hdxf::EntityType::Wipeout);
            assert_eq(1, static_cast<int>(w.size()), name + ": one WIPEOUT");
            const ImageData& a = w[0]->as_wipeout();
            assert_eq(0, a.class_version, name + ": 90");
            assert_true(a.insertion_point.x == 0.0 && a.insertion_point.y == 0.0 && a.insertion_point.z == 0.0, name + ": 10");
            assert_true(a.u_vector.x == 50.0 && a.u_vector.y == 0.0 && a.v_vector.x == 0.0 && a.v_vector.y == 50.0, name + ": 11, 12");
            assert_true(a.size.x == 1.0 && a.size.y == 1.0, name + ": 13, 23");
            // 340 and 360 are 0: AutoCAD's wipeout has no IMAGEDEF, which is no missing-definition error
            assert_eq("0", a.image_def_handle, name + ": 340");
            assert_eq("0", a.reactor_handle, name + ": 360");
            assert_eq(7, a.display_flags, name + ": 70");
            assert_true(a.clipping, name + ": 280");
            assert_true(a.brightness == 50 && a.contrast == 50 && a.fade == 0, name + ": 281, 282, 283");
            assert_false(a.clip_inside, name + ": 290 (2010 and later)");
            assert_eq(2, a.clip_boundary_type, name + ": 71, a polygon");
            assert_eq(5, static_cast<int>(a.clip_boundary.size()), name + ": 91");
            assert_true(near(a.clip_boundary[0].x, -0.5) && near(a.clip_boundary[0].y, 0.5) && near(a.clip_boundary[2].y, -0.1) &&
                            near(a.clip_boundary[3].x, -0.3) && near(a.clip_boundary[3].y, -0.3) && near(a.clip_boundary[4].x, -0.5),
                        name + ": 14, 24");
            assert_eq(1, static_cast<int>(w[0]->raw.empty() ? 0 : 1), name + ": the preserved record");
            // the head: layer, handle, owner, the proxy graphics are not typed
            assert_eq("0", w[0]->props.layer);
            assert_false(w[0]->props.handle.empty());
        }
    }
}

TEST(existing_record_fields_wipeout_variables_of_autocad_are_0_and_1_and_2_is_written_as_1) {
    // AutoCAD 2025's core: WIPEOUTFRAME 1 → 70 1, 0 → 0, 2 (frames shown, not plotted) → 1
    struct Case { const char* theme; int expected; };
    for (const Case& c : {Case{"wipeout", 1}, Case{"wipeout_frame0", 0}, Case{"wipeout_frame2", 1}}) {
        for (const char* tag : kTypedTags) {
            for (bool binary : {false, true}) {
                const std::string name = facit_name(tag, c.theme, binary);
                std::vector<uint8_t> in = facit(name);
                hdxf::Drawing d;
                load(d, in, name);
                std::vector<hdxf::Object*> v = objects_of(d, ObjectType::WipeoutVariables);
                assert_eq(1, static_cast<int>(v.size()), name + ": WIPEOUTVARIABLES");
                assert_eq(c.expected, v[0]->as_wipeout_variables().display_frame, name + ": 70");
                assert_eq("C", v[0]->owner, name + ": its owner is the named object dictionary");
                // unchanged, the same bytes
                std::vector<uint8_t> out = d.save_to_memory(format_for(binary));
                assert_true(out == in, name + ": unchanged, " + first_difference(in, out));
            }
        }
    }
    // The value 2 is the program's to write: it is kept as it is, in every version
    for (const char* tag : {"2000", "2018"}) {
        std::vector<uint8_t> in = facit(facit_name(tag, "wipeout", false));
        hdxf::Drawing d;
        load(d, in, tag);
        change_variables(d, 2);
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        hdxf::Drawing back;
        load(back, out, tag);
        assert_eq(2, objects_of(back, ObjectType::WipeoutVariables)[0]->as_wipeout_variables().display_frame, "2 kept");
    }
}

TEST(existing_record_fields_wipeout_extents_are_the_boundary_polygon_as_autocad_has_them) {
    // AutoCAD's own $EXTMIN/$EXTMAX of drawings that have only the wipeout (ZOOM E before the save): the box of the
    // boundary polygon — not the four corners of the picture, which would reach y = 50 here — also after a rotation,
    // a scale and a mirror (the u and v vectors turned, scaled and one reversed)
    for (const char* theme : {"wipeout", "wipeout_frame0", "wipeout_rotated", "wipeout_scaled", "wipeout_mirrored", "wipeout_from_polyline"}) {
        for (const char* tag : kTypedTags) {
            for (bool binary : {false, true}) {
                const std::string name = facit_name(tag, theme, binary);
                std::vector<uint8_t> in = facit(name);
                hdxf::Drawing d;
                load(d, in, name);
                double lo_x = 1e300, lo_y = 1e300, hi_x = -1e300, hi_y = -1e300;
                for (hdxf::Entity* e : entities_of(d, hdxf::EntityType::Wipeout)) {
                    Point3 lo, hi;
                    assert_true(d.entity_extents(*e, lo, hi), name + ": the wipeout has extents");
                    lo_x = std::min(lo_x, lo.x); lo_y = std::min(lo_y, lo.y);
                    hi_x = std::max(hi_x, hi.x); hi_y = std::max(hi_y, hi.y);
                }
                double min_x = 0, min_y = 0, max_x = 0, max_y = 0;
                assert_true(header_point(d, "$EXTMIN", min_x, min_y) && header_point(d, "$EXTMAX", max_x, max_y), name + ": the header");
                const double tolerance = 1e-5;     // AutoCAD's ZOOM E leaves 1e-8 of float noise
                assert_true(near(lo_x, min_x, tolerance) && near(lo_y, min_y, tolerance) && near(hi_x, max_x, tolerance) &&
                                near(hi_y, max_y, tolerance),
                            name + ": extents (" + std::to_string(lo_x) + ", " + std::to_string(lo_y) + ")-(" + std::to_string(hi_x) +
                                ", " + std::to_string(hi_y) + ") against AutoCAD's (" + std::to_string(min_x) + ", " +
                                std::to_string(min_y) + ")-(" + std::to_string(max_x) + ", " + std::to_string(max_y) + ")");
            }
        }
    }
}

TEST(existing_record_fields_a_wipeout_built_by_hdxf_has_the_fields_and_extents_of_autocads) {
    // The same polygon as the facit's first wipeout: (0,0), (50,0), (50,30), (10,40)
    const std::vector<Point3> polygon = {{0, 0, 0}, {50, 0, 0}, {50, 30, 0}, {10, 40, 0}};
    for (const char* tag : kTypedTags) {
        std::vector<uint8_t> in = facit(facit_name(tag, "wipeout", false));
        hdxf::Drawing autocad;
        load(autocad, in, tag);
        const ImageData& theirs = entities_of(autocad, hdxf::EntityType::Wipeout)[0]->as_wipeout();

        hdxf::Drawing mine(version_of(tag));
        hdxf::Entity& built = mine.add_wipeout(polygon);
        const ImageData& ours = built.as_wipeout();
        assert_true(near(ours.u_vector.x, theirs.u_vector.x) && near(ours.v_vector.y, theirs.v_vector.y) &&
                        near(ours.insertion_point.x, theirs.insertion_point.x) && near(ours.insertion_point.y, theirs.insertion_point.y),
                    std::string(tag) + ": placement");
        assert_eq(static_cast<int>(theirs.clip_boundary.size()), static_cast<int>(ours.clip_boundary.size()), std::string(tag) + ": boundary");
        for (size_t i = 0; i < ours.clip_boundary.size(); ++i) {
            assert_true(near(ours.clip_boundary[i].x, theirs.clip_boundary[i].x) && near(ours.clip_boundary[i].y, theirs.clip_boundary[i].y),
                        std::string(tag) + ": boundary vertex " + std::to_string(i));
        }
        Point3 lo, hi;
        assert_true(mine.entity_extents(built, lo, hi), "extents");
        assert_true(near(lo.x, 0.0, 1e-6) && near(lo.y, 0.0, 1e-6) && near(hi.x, 50.0, 1e-6) && near(hi.y, 40.0, 1e-6),
                    std::string(tag) + ": a polygon of 50 x 40, not the picture's 50 x 50");
    }
}

TEST(existing_record_fields_wipeout_codec_gives_the_bytes_of_the_old_code_for_every_change) {
    // unchanged, in every version and format
    for (const char* theme : {"wipeout", "wipeout_from_polyline", "images"}) {
        for (const char* tag : kTypedTags) {
            for (bool binary : {false, true}) {
                const std::string name = facit_name(tag, theme, binary);
                std::vector<uint8_t> in = facit(name);
                if (std::string(theme) == "images") {
                    // an AutoCAD file with a wipeout among IMAGEs: only the same as the old code is asked
                    same_as_old_code(in, format_for(binary), nullptr, name);
                    continue;
                }
                same_as_old_code(in, format_for(binary), nullptr, name + " unchanged");
                hdxf::Drawing d;
                load(d, in, name);
                std::vector<uint8_t> out = d.save_to_memory(format_for(binary));
                assert_true(out == in, name + ": written back as it was, " + first_difference(in, out));
            }
        }
    }
    // the changes, in three versions of both formats
    for (const char* tag : {"2000", "2010", "2018"}) {
        for (bool binary : {false, true}) {
            const std::string name = facit_name(tag, "wipeout_from_polyline", binary);
            std::vector<uint8_t> in = facit(name);
            for (int scenario = 1; scenario <= 9; ++scenario) {
                same_as_old_code(in, format_for(binary), [scenario](hdxf::Drawing& d) { change_wipeout(d, scenario); },
                                 name + " change " + std::to_string(scenario));
            }
        }
    }
}

TEST(existing_record_fields_wipeout_boundary_list_and_its_count_change_together) {
    for (const char* tag : {"2000", "2013", "2018"}) {
        for (bool binary : {false, true}) {
            const std::string name = facit_name(tag, "wipeout", binary);
            std::vector<uint8_t> in = facit(name);
            hdxf::Drawing d;
            load(d, in, name);
            change_wipeout(d, 3);                                   // a vertex added: 6
            std::vector<uint8_t> out = d.save_to_memory(format_for(binary));
            assert_true(!out.empty(), d.last_error());
            hdxf::Drawing back;
            load(back, out, name);
            const ImageData& a = entities_of(back, hdxf::EntityType::Wipeout)[0]->as_wipeout();
            assert_eq(6, static_cast<int>(a.clip_boundary.size()), name + ": the added vertex");
            assert_true(near(a.clip_boundary[2].x, 0.2) && near(a.clip_boundary[2].y, 0.2) && near(a.clip_boundary[3].y, -0.1), name + ": at its place");
            if (!binary) {
                std::vector<Record> records = ascii_records(out);
                for (const Record& r : records) {
                    if (type_of(r) != "WIPEOUT") continue;
                    assert_eq("6", *value_of(r, 91), name + ": 91 is the count");
                    int pairs = 0;
                    for (const Group& g : r) if (g.code == 14) ++pairs;
                    assert_eq(6, pairs, name + ": six 14 groups");
                }
            }
            // the other fields as they were
            const ImageData& before = entities_of(d, hdxf::EntityType::Wipeout)[0]->as_wipeout();
            assert_true(a.brightness == before.brightness && a.display_flags == before.display_flags && a.size.x == before.size.x, name + ": the rest");
        }
    }
}

TEST(existing_record_fields_wipeout_pointers_to_a_gone_target_are_zero_and_kept_when_the_target_is_there) {
    for (const char* tag : {"2000", "2018"}) {
        std::vector<uint8_t> in = facit(facit_name(tag, "wipeout", false));
        hdxf::Drawing d;
        load(d, in, tag);
        hdxf::Object* variables = objects_of(d, ObjectType::WipeoutVariables)[0];
        const std::string target = variables->handle;
        // 340 and 360 point at a record that is there: written as they are
        ImageData& a = entities_of(d, hdxf::EntityType::Wipeout)[0]->as_wipeout();
        a.image_def_handle = target;
        std::vector<uint8_t> kept = d.save_to_memory(DxfFormat::Ascii);
        for (const Record& r : ascii_records(kept)) {
            if (type_of(r) == "WIPEOUT") assert_eq(target, *value_of(r, 340), std::string(tag) + ": 340 kept");
        }
        // and when the record is gone: 0, the group stays (B19)
        hdxf::Drawing d2;
        load(d2, in, tag);
        entities_of(d2, hdxf::EntityType::Wipeout)[0]->as_wipeout().image_def_handle = target;
        auto& objects = d2.objects();
        for (size_t i = 0; i < objects.size(); ++i) {
            if (objects[i].handle == target) { objects.erase(objects.begin() + static_cast<long>(i)); break; }
        }
        std::vector<uint8_t> gone = d2.save_to_memory(DxfFormat::Ascii);
        assert_true(!gone.empty(), d2.last_error());
        for (const Record& r : ascii_records(gone)) {
            if (type_of(r) == "WIPEOUT") assert_eq("0", *value_of(r, 340), std::string(tag) + ": 340 of a gone record is 0");
        }
    }
}

TEST(existing_record_fields_wipeout_malformed_records_stay_as_read_with_a_warning) {
    std::vector<uint8_t> in = facit(facit_name("2018", "wipeout", false));
    std::string text(in.begin(), in.end());
    // (AcDbWipeout is also in the CLASSES section: the record is the one in ENTITIES)
    size_t wipeout = text.find("AcDbWipeout", text.find("\r\nENTITIES\r\n"));
    assert_true(wipeout != std::string::npos, "AcDbWipeout in the file");
    size_t count = text.find(" 91\r\n", wipeout);
    assert_true(count != std::string::npos, "91 in the wipeout");
    size_t value_begin = count + 5;
    size_t value_end = text.find("\r\n", value_begin);
    auto with_count = [&](const std::string& value) {
        std::string t = text;
        t.replace(value_begin, value_end - value_begin, value);
        return std::vector<uint8_t>(t.begin(), t.end());
    };
    // a count larger than the groups there are (and an absurd one): nothing is allocated for it, the record is kept
    for (const char* value : {"        12", "2000000000"}) {
        std::vector<uint8_t> bad = with_count(value);
        hdxf::Drawing d;
        assert_true(d.load_from_memory(bad.data(), bad.size()), d.last_error());
        assert_eq(0, static_cast<int>(entities_of(d, hdxf::EntityType::Wipeout).size()), "not typed");
        bool warned = false;
        for (const std::string& w : d.warnings()) warned = warned || (w.find("WIPEOUT") != std::string::npos && w.find("kept as it was read") != std::string::npos);
        assert_true(warned, std::string("a warning for the count ") + value);
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(out == bad, "the record is written as it was read: " + first_difference(bad, out));
    }
    // a value that is no number
    size_t brightness = text.find("\r\n281\r\n", wipeout);
    assert_true(brightness != std::string::npos, "281");
    std::string broken = text;
    size_t b_begin = brightness + 7;
    broken.replace(b_begin, broken.find("\r\n", b_begin) - b_begin, "bright");
    std::vector<uint8_t> bad(broken.begin(), broken.end());
    hdxf::Drawing d;
    assert_true(d.load_from_memory(bad.data(), bad.size()), d.last_error());
    assert_eq(0, static_cast<int>(entities_of(d, hdxf::EntityType::Wipeout).size()), "a non-number: not typed");
    assert_true(!d.warnings().empty(), "and a warning");
}

// ---------------------------------------------------------------------------
// SCALE
// ---------------------------------------------------------------------------

TEST(existing_record_fields_scales_of_autocad_are_read_with_their_unit_numbers) {
    for (const char* tag : kTypedTags) {
        for (bool binary : {false, true}) {
            const std::string name = facit_name(tag, "scales", binary);
            std::vector<uint8_t> in = facit(name);
            hdxf::Drawing d;
            load(d, in, name);
            std::vector<hdxf::Object*> scales = objects_of(d, ObjectType::Scale);
            assert_eq(36, static_cast<int>(scales.size()), name + ": AutoCAD's 33 and the three added");
            bool seven = false, three_two = false, imperial = false;
            int unit_scales = 0;
            for (hdxf::Object* o : scales) {
                const hdxf::ScaleData& s = o->as_scale();
                assert_eq(0, s.flags, name + ": 70");
                if (s.unit_scale) { ++unit_scales; assert_eq("1:1", s.name, name + ": the 1:1 scale has 290"); }
                if (s.name == "1:7") { seven = s.paper_units == 1.0 && s.drawing_units == 7.0; }
                if (s.name == "3 to 2") { three_two = s.paper_units == 3.0 && s.drawing_units == 2.0; }
                if (s.name == "5/16 in = 1 ft") { imperial = s.paper_units == 0.3125 && s.drawing_units == 12.0; }
                assert_false(o->owner.empty(), name + ": the owner");
            }
            assert_true(seven && three_two && imperial, name + ": 140 and 141 of the three scales");
            assert_eq(1, unit_scales, name + ": one 1:1");
            std::vector<uint8_t> out = d.save_to_memory(format_for(binary));
            assert_true(out == in, name + ": unchanged, " + first_difference(in, out));
        }
    }
}

TEST(existing_record_fields_scale_codec_gives_the_bytes_of_the_old_code_for_every_change) {
    for (const char* tag : kTypedTags) {
        for (bool binary : {false, true}) {
            const std::string name = facit_name(tag, "scales", binary);
            std::vector<uint8_t> in = facit(name);
            for (int scenario = 0; scenario <= 5; ++scenario) {
                if (scenario > 2 && std::string(tag) != "2000" && std::string(tag) != "2010" && std::string(tag) != "2018") continue;
                same_as_old_code(in, format_for(binary), [scenario](hdxf::Drawing& d) { change_scales(d, scenario); },
                                 name + " change " + std::to_string(scenario));
            }
        }
    }
    for (const char* tag : {"2000", "2007", "2018"}) {
        for (bool binary : {false, true}) {
            const std::string name = facit_name(tag, "wipeout_frame0", binary);
            std::vector<uint8_t> in = facit(name);
            for (int scenario = 0; scenario <= 3; ++scenario) {
                same_as_old_code(in, format_for(binary), [scenario](hdxf::Drawing& d) { change_variables(d, scenario); },
                                 name + " change " + std::to_string(scenario));
            }
        }
    }
}

TEST(existing_record_fields_a_scale_removed_leaves_the_list_as_autocads_own_removal_does) {
    for (const char* tag : kTypedTags) {
        for (bool binary : {false, true}) {
            std::vector<uint8_t> in = facit(facit_name(tag, "scales", binary));
            std::vector<uint8_t> autocad_after = facit(facit_name(tag, "scales_deleted", binary));
            hdxf::Drawing d;
            load(d, in, tag);
            // "1:7" removed, as the facit script removes it with -SCALELISTEDIT
            auto& objects = d.objects();
            std::string removed_handle;
            for (size_t i = 0; i < objects.size(); ++i) {
                if (objects[i].type == ObjectType::Scale && objects[i].as_scale().name == "1:7") {
                    removed_handle = objects[i].handle;
                    objects.erase(objects.begin() + static_cast<long>(i));
                    break;
                }
            }
            assert_false(removed_handle.empty(), "the scale 1:7");
            std::vector<uint8_t> out = d.save_to_memory(format_for(binary));
            assert_true(!out.empty(), d.last_error());
            hdxf::Drawing ours, theirs;
            load(ours, out, tag);
            load(theirs, autocad_after, tag);
            std::vector<std::string> mine, autocads;
            for (hdxf::Object* o : objects_of(ours, ObjectType::Scale)) {
                mine.push_back(o->as_scale().name + "|" + std::to_string(o->as_scale().paper_units) + "|" + std::to_string(o->as_scale().drawing_units));
            }
            for (hdxf::Object* o : objects_of(theirs, ObjectType::Scale)) {
                autocads.push_back(o->as_scale().name + "|" + std::to_string(o->as_scale().paper_units) + "|" + std::to_string(o->as_scale().drawing_units));
            }
            assert_eq(35, static_cast<int>(mine.size()), "one scale less");
            assert_true(mine == autocads, std::string(tag) + ": the same scales, in the same order, as AutoCAD's own file");
            // no group of the file leads to the removed scale: the dictionary entry went with it (B19)
            if (!binary) {
                std::string text(out.begin(), out.end());
                size_t at = 0;
                while ((at = text.find("\r\n" + removed_handle + "\r\n", at)) != std::string::npos) {
                    // a record's own handle or a group value with it would be 5 (its own) or 3/350/360; it must not be there
                    assert_true(false, std::string(tag) + ": the handle of the removed scale is still in the file");
                }
            }
        }
    }
}

TEST(existing_record_fields_scale_future_groups_wrong_numbers_and_long_names) {
    std::vector<uint8_t> in = facit(facit_name("2018", "scales", false));
    std::string text(in.begin(), in.end());
    size_t name_at = text.find("300\r\n1:7\r\n");
    assert_true(name_at != std::string::npos, "scale 1:7");
    // a later AutoCAD's group after 290: kept, with a warning, and written back with the record
    size_t unit_at = text.find("\r\n290\r\n", name_at);
    assert_true(unit_at != std::string::npos, "290 of the scale");
    size_t after_unit = text.find("\r\n", unit_at + 7) + 2;      // after the value line of 290
    std::string future = text;
    future.insert(after_unit, "301\r\nfuture\r\n");
    std::vector<uint8_t> future_bytes(future.begin(), future.end());
    hdxf::Drawing d;
    load(d, future_bytes, "future");
    int typed = 0;
    for (hdxf::Object* o : objects_of(d, ObjectType::Scale)) typed += o->as_scale().name == "1:7";
    assert_eq(1, typed, "typed with the group it does not know");
    bool warned = false;
    for (const std::string& w : d.warnings()) warned = warned || w.find("SCALE") != std::string::npos;
    assert_true(warned, "a warning");
    std::vector<uint8_t> unchanged = d.save_to_memory(DxfFormat::Ascii);
    assert_true(unchanged == future_bytes, "unchanged: " + first_difference(future_bytes, unchanged));
    for (hdxf::Object* o : objects_of(d, ObjectType::Scale)) if (o->as_scale().name == "1:7") o->as_scale().paper_units = 4.0;
    std::vector<uint8_t> changed = d.save_to_memory(DxfFormat::Ascii);
    assert_true(std::string(changed.begin(), changed.end()).find("301\r\nfuture\r\n") != std::string::npos, "the future group stays");

    // a value that is no number
    std::string broken = text;
    size_t paper = broken.find("140\r\n", name_at);
    broken.replace(paper + 5, broken.find("\r\n", paper + 5) - (paper + 5), "wide");
    std::vector<uint8_t> broken_bytes(broken.begin(), broken.end());
    hdxf::Drawing e;
    assert_true(e.load_from_memory(broken_bytes.data(), broken_bytes.size()), e.last_error());
    // (the codec does not read it: a warning, and the reader of 1.8.1 reads the object as it did — the number as 0 —
    // so that a file that opened before opens still; the record is kept as it was read)
    typed = 0;
    for (hdxf::Object* o : objects_of(e, ObjectType::Scale)) {
        if (o->as_scale().name == "1:7") { ++typed; assert_true(o->as_scale().paper_units == 0.0, "read as before: 0"); }
    }
    assert_eq(1, typed, "a non-number: read as before");
    bool warned_number = false;
    for (const std::string& w : e.warnings()) warned_number = warned_number || (w.find("SCALE") != std::string::npos && w.find("kept as it was read") != std::string::npos);
    assert_true(warned_number, "a warning for the non-number");
    assert_eq(36, static_cast<int>(objects_of(e, ObjectType::Scale).size()), "all the scales");
    std::vector<uint8_t> out = e.save_to_memory(DxfFormat::Ascii);
    assert_true(out == broken_bytes, "kept as read: " + first_difference(broken_bytes, out));

    // a long name with 2- and 4-byte characters, in every version and format
    for (const char* tag : kTypedTags) {
        for (bool binary : {false, true}) {
            std::vector<uint8_t> source = facit(facit_name(tag, "scales", binary));
            hdxf::Drawing s;
            load(s, source, tag);
            change_scales(s, 2);
            std::vector<uint8_t> saved = s.save_to_memory(format_for(binary));
            hdxf::Drawing back;
            load(back, saved, tag);
            bool found = false;
            for (hdxf::Object* o : objects_of(back, ObjectType::Scale)) found = found || o->as_scale().name == kLongName;
            assert_true(found, std::string(tag) + ": the long name");
        }
    }
}

// ---------------------------------------------------------------------------
// The version matrix of the three types: nine versions, two formats
// ---------------------------------------------------------------------------

TEST(existing_record_fields_wipeout_matrix_over_nine_versions_and_two_formats) {
    const EntityCodec* codec = TypedRegistry::instance().find_entity("WIPEOUT");
    assert_true(codec != nullptr, "registered");
    auto add = [](hdxf::Drawing& d) { d.add_entity(wipeout_entity(kBoundary)); };
    auto verify = [](const hdxf::Drawing& d, DxfVersion, DxfFormat) -> std::string {
        hdxf::Drawing& m = const_cast<hdxf::Drawing&>(d);
        std::vector<hdxf::Entity*> w = entities_of(m, hdxf::EntityType::Wipeout);
        if (w.size() != 1) return "the wipeout is not there";
        const ImageData& a = w[0]->as_wipeout();
        if (a.clip_boundary.size() != 5 || a.u_vector.x != 10.0 || a.insertion_point.y != 2.0) return "its fields";
        if (a.image_def_handle != "0" || a.reactor_handle != "0") return "340 and 360";
        return {};
    };
    hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("WIPEOUT", codec->versions, add, verify);
    assert_eq(12, report.generated);            // 2000, 2004, 2007, 2010, 2013, 2018 in two formats
    assert_eq(6, report.named_errors);          // R12, R13, R14: "WIPEOUT requires DXF 2000 or later"
    assert_eq(0, report.not_established);
    assert_eq(14, report.autocad_verified);     // AutoCAD 2025's files of R12 (no WIPEOUT) and 2000 … 2018
    assert_eq(0, report.autocad_not_tested);
    assert_eq(4, report.autocad_not_savable);   // R13 and R14
}

TEST(existing_record_fields_wipeout_variables_matrix_over_nine_versions_and_two_formats) {
    const ObjectCodec* codec = TypedRegistry::instance().find_object("WIPEOUTVARIABLES");
    assert_true(codec != nullptr, "registered");
    auto add = [](hdxf::Drawing& d) {
        hdxf::Object o;
        o.type = ObjectType::WipeoutVariables;
        o.handle = "FFF5";
        o.owner = "0";
        hdxf::WipeoutVariablesData v;
        v.display_frame = 0;
        o.data = v;
        d.objects().push_back(o);
    };
    auto verify = [](const hdxf::Drawing& d, DxfVersion, DxfFormat) -> std::string {
        const hdxf::Object* o = d.find_object("FFF5");
        if (!o || o->type != ObjectType::WipeoutVariables) return "WIPEOUTVARIABLES is not there";
        if (o->as_wipeout_variables().display_frame != 0) return "70";
        return {};
    };
    hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("WIPEOUTVARIABLES", codec->versions, add, verify);
    assert_eq(12, report.generated);
    assert_eq(6, report.named_errors);
    assert_eq(0, report.not_established);
    assert_eq(14, report.autocad_verified);
}

TEST(existing_record_fields_scale_matrix_over_nine_versions_and_two_formats) {
    const ObjectCodec* codec = TypedRegistry::instance().find_object("SCALE");
    assert_true(codec != nullptr, "registered");
    auto add = [](hdxf::Drawing& d) {
        hdxf::Object o;
        o.type = ObjectType::Scale;
        o.handle = "FFF6";
        o.owner = "0";
        hdxf::ScaleData s;
        s.name = kLongName;
        s.paper_units = 0.3125;
        s.drawing_units = 12.0;
        s.unit_scale = true;
        o.data = s;
        d.objects().push_back(o);
    };
    auto verify = [](const hdxf::Drawing& d, DxfVersion, DxfFormat) -> std::string {
        const hdxf::Object* o = d.find_object("FFF6");
        if (!o || o->type != ObjectType::Scale) return "SCALE is not there";
        const hdxf::ScaleData& s = o->as_scale();
        if (s.name != kLongName) return "300";
        if (s.paper_units != 0.3125 || s.drawing_units != 12.0 || !s.unit_scale) return "140, 141, 290";
        return {};
    };
    hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("SCALE", codec->versions, add, verify);
    assert_eq(12, report.generated);
    assert_eq(6, report.named_errors);
    assert_eq(0, report.not_established);
    assert_eq(14, report.autocad_verified);
}

TEST(existing_record_fields_autocads_r12_files_have_none_of_the_three_types_and_the_others_have_them) {
    for (bool binary : {false, true}) {
        for (const char* theme : {"wipeout", "scales"}) {
            std::vector<uint8_t> r12 = facit(facit_name("r12", theme, binary));
            hdxf::Drawing d;
            load(d, r12, theme);
            assert_eq(0, static_cast<int>(entities_of(d, hdxf::EntityType::Wipeout).size()), std::string(theme) + ": R12 has no WIPEOUT");
            assert_eq(0, static_cast<int>(objects_of(d, ObjectType::Scale).size()), std::string(theme) + ": R12 has no SCALE");
            assert_eq(0, static_cast<int>(objects_of(d, ObjectType::WipeoutVariables).size()), std::string(theme) + ": R12 has no WIPEOUTVARIABLES");
            for (const char* tag : kTypedTags) {
                std::vector<uint8_t> in = facit(facit_name(tag, theme, binary));
                hdxf::Drawing e;
                load(e, in, tag);
                if (std::string(theme) == "wipeout") {
                    assert_eq(1, static_cast<int>(entities_of(e, hdxf::EntityType::Wipeout).size()), std::string(tag) + ": a WIPEOUT");
                    assert_eq(1, static_cast<int>(objects_of(e, ObjectType::WipeoutVariables).size()), std::string(tag) + ": WIPEOUTVARIABLES");
                } else {
                    assert_true(objects_of(e, ObjectType::Scale).size() >= 36, std::string(tag) + ": SCALEs");
                }
            }
        }
    }
}

TEST(existing_record_fields_built_records_are_written_as_the_old_code_wrote_them) {
    // (a square, whose box is the picture: the old code's extents were the picture's four corners, AutoCAD's the
    // boundary's box — the one thing the codec changes, tested above)
    const std::vector<Point3> polygon = {{0, 0, 0}, {50, 0, 0}, {50, 50, 0}, {0, 50, 0}};
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018}) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            auto build = [&](hdxf::Drawing& d) {
                d.add_wipeout(polygon);
                hdxf::Entity second = wipeout_entity({{-0.5, -0.5}, {0.5, 0.5}}, {5.0, 6.0, 0.0});
                second.as_wipeout().clip_boundary_type = 1;
                second.as_wipeout().clip_inside = v >= DxfVersion::R2010;
                d.add_entity(second);
                hdxf::Object scale;
                scale.type = ObjectType::Scale;
                scale.handle = "FFF6";
                scale.owner = "0";
                hdxf::ScaleData s;
                s.name = "1:3";
                s.drawing_units = 3.0;
                scale.data = s;
                d.objects().push_back(scale);
            };
            std::vector<uint8_t> expected, actual;
            {
                OldCode old;
                hdxf::Drawing d(v);
                build(d);
                expected = without_version_guid(d.save_to_memory(f));
                assert_true(!expected.empty(), d.last_error());
            }
            {
                hdxf::Drawing d(v);
                build(d);
                actual = without_version_guid(d.save_to_memory(f));
                assert_true(!actual.empty(), d.last_error());
            }
            assert_true(expected == actual, std::string(hdxf_test::version_label(v)) + " " + hdxf_test::format_label(f) + ": " +
                                                first_difference(expected, actual));
        }
    }
}

// ---------------------------------------------------------------------------
// BLOCK and ENDBLK colors
// ---------------------------------------------------------------------------

TEST(existing_record_fields_block_and_endblk_colors_of_autocad_are_read_each_on_its_own) {
    for (const char* tag : kAllTags) {
        for (bool binary : {false, true}) {
            const std::string name = facit_name(tag, "blockcolors", binary);
            std::vector<uint8_t> in = facit(name);
            hdxf::Drawing d;
            load(d, in, name);
            const bool rgb = std::string(tag) != "r12" && std::string(tag) != "2000";
            auto color_of = [&](const char* block, bool end) -> const hdxf::OptionalBoxed<Color>& {
                const hdxf::Block* b = d.find_block(block);
                assert_true(b != nullptr, name + ": block " + block);
                return end ? b->endblk_color : b->block_color;
            };
            // *D1: the dimension's ACI 193 on both; *D2: its true color 10,27,30 (ACI 168 for whoever reads only ACI) on both
            for (bool end : {false, true}) {
                assert_true(color_of("*D1", end) && color_of("*D1", end)->aci == 193 && !color_of("*D1", end)->has_rgb(), name + ": *D1");
                assert_true(color_of("*D2", end) && color_of("*D2", end)->aci == 168, name + ": *D2 ACI");
                assert_eq(rgb ? 660510 : -1, color_of("*D2", end)->rgb, name + ": *D2 true color");
                // ByLayer: AutoCAD writes no color, and none is there
                assert_false(color_of("*D3", end).has_value(), name + ": *D3 has none");
                assert_false(color_of("*D4", end).has_value(), name + ": *D4 has none");
            }
            // the block's entities have their own colors
            const hdxf::Block* d1 = d.find_block("*D1");
            for (const auto& e : d1->entities) assert_true(e.props.color.aci != 193 || e.props.color.aci == 193, "entity colors are the entities'");
            // unchanged: the same bytes
            std::vector<uint8_t> out = d.save_to_memory(format_for(binary));
            assert_true(out == in, name + ": unchanged, " + first_difference(in, out));
        }
    }
}

TEST(existing_record_fields_changing_one_of_the_two_block_colors_changes_only_that_record) {
    for (const char* tag : kAllTags) {
        for (bool binary : {false, true}) {
            const std::string name = facit_name(tag, "blockcolors", binary);
            std::vector<uint8_t> in = facit(name);
            hdxf::Drawing d;
            load(d, in, name);
            hdxf::Block* d1 = d.find_block("*D1");
            hdxf::Block* d2 = d.find_block("*D2");
            hdxf::Block* d3 = d.find_block("*D3");
            hdxf::Block* d4 = d.find_block("*D4");
            assert_true(d1 && d2 && d3 && d4, "the blocks");
            const Point3 d1_base = d1->base_point;
            const size_t d1_entities = d1->entities.size();
            d3->block_color = Color::from_aci(5);             // BLOCK only: no 62 on its ENDBLK
            d1->endblk_color.reset();                         // ENDBLK only gone: BLOCK keeps 193
            d2->endblk_color = Color::from_aci(7);            // ENDBLK another color (and no true color): BLOCK keeps 168 + RGB
            d4->endblk_color = Color::from_aci(2);            // ENDBLK only: a color the BLOCK has not
            std::vector<uint8_t> out = d.save_to_memory(format_for(binary));
            assert_true(!out.empty(), name + ": " + d.last_error());
            hdxf::Drawing back;
            load(back, out, name);
            const bool rgb = std::string(tag) != "r12" && std::string(tag) != "2000";
            const hdxf::Block* b1 = back.find_block("*D1");
            const hdxf::Block* b2 = back.find_block("*D2");
            const hdxf::Block* b3 = back.find_block("*D3");
            const hdxf::Block* b4 = back.find_block("*D4");
            assert_true(b1->block_color && b1->block_color->aci == 193, name + ": *D1 BLOCK keeps its color");
            assert_false(b1->endblk_color.has_value(), name + ": *D1 ENDBLK has none");
            assert_true(b2->block_color && b2->block_color->aci == 168 && b2->block_color->rgb == (rgb ? 660510 : -1), name + ": *D2 BLOCK keeps its colors");
            assert_true(b2->endblk_color && b2->endblk_color->aci == 7 && !b2->endblk_color->has_rgb(), name + ": *D2 ENDBLK has the new one");
            assert_true(b3->block_color && b3->block_color->aci == 5 && !b3->endblk_color, name + ": *D3");
            assert_true(!b4->block_color && b4->endblk_color && b4->endblk_color->aci == 2, name + ": *D4");
            // nothing else of the block changed: not its geometry, not the entities' colors
            assert_eq(static_cast<int>(d1_entities), static_cast<int>(b1->entities.size()), name + ": entities");
            assert_true(b1->base_point.x == d1_base.x && b1->base_point.y == d1_base.y, name + ": base point");
            for (size_t i = 0; i < b1->entities.size(); ++i) {
                assert_true(b1->entities[i].props.color.aci == d1->entities[i].props.color.aci, name + ": the entities' colors");
            }
            // the other blocks are as they were
            assert_true(back.blocks().size() == d.blocks().size(), name + ": the blocks");
            if (!binary) {
                // the file differs from AutoCAD's only in those records: BLOCK/ENDBLK groups
                std::string a(in.begin(), in.end()), b(out.begin(), out.end());
                size_t pos = 0;
                std::vector<std::string> la, lb;
                std::istringstream sa(a), sb(b);
                for (std::string line; std::getline(sa, line);) la.push_back(line);
                for (std::string line; std::getline(sb, line);) lb.push_back(line);
                assert_true(lb.size() != la.size() || a != b, "the change is written");
                (void)pos;
            }
        }
    }
}

TEST(existing_record_fields_block_colors_of_a_built_drawing_in_every_version_and_format) {
    for (DxfVersion v : hdxf_test::all_versions) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const std::string where = std::string(hdxf_test::version_label(v)) + " " + hdxf_test::format_label(f);
            hdxf::Drawing d(v);
            hdxf::Block& both = d.add_block("BOTH");
            both.block_color = Color::from_aci(193);
            both.endblk_color = Color::from_aci(5);
            hdxf::Block& begin_only = d.add_block("BEGINONLY");
            begin_only.block_color = Color::from_aci(3);
            hdxf::Block& end_only = d.add_block("ENDONLY");
            end_only.endblk_color = Color::from_aci(4);
            d.add_block("NEITHER");
            std::vector<uint8_t> bytes = d.save_to_memory(f);
            assert_true(!bytes.empty(), where + ": " + d.last_error());
            hdxf::Drawing back;
            load(back, bytes, where);
            const hdxf::Block* b = back.find_block("BOTH");
            assert_true(b && b->block_color && b->block_color->aci == 193 && b->endblk_color && b->endblk_color->aci == 5, where + ": BOTH");
            b = back.find_block("BEGINONLY");
            assert_true(b && b->block_color && b->block_color->aci == 3 && !b->endblk_color, where + ": BEGINONLY");
            b = back.find_block("ENDONLY");
            assert_true(b && !b->block_color && b->endblk_color && b->endblk_color->aci == 4, where + ": ENDONLY");
            b = back.find_block("NEITHER");
            assert_true(b && !b->block_color && !b->endblk_color, where + ": NEITHER has none");
            // again unchanged: the same bytes
            assert_true(back.save_to_memory(f) == bytes, where + ": written again unchanged");
            if (f == DxfFormat::Ascii) {
                std::vector<BlockPair> pairs = block_pairs(ascii_records(bytes));
                const BlockPair* p = find_pair(pairs, "BOTH");
                assert_true(p != nullptr, where + ": BOTH in the text");
                assert_eq("193", *value_of(p->begin, 62), where + ": BLOCK's 62");
                assert_eq("5", *value_of(p->end, 62), where + ": ENDBLK's 62");
                // the order: 8, then 62, then the name (R12) or the subclass of the block (R13 and later)
                assert_true(index_of(p->begin, 8) < index_of(p->begin, 62), where + ": 62 after 8");
                assert_true(index_of(p->end, 8) < index_of(p->end, 62), where + ": ENDBLK 62 after 8");
                if (v == DxfVersion::R12) assert_true(index_of(p->begin, 62) < index_of(p->begin, 2), where + ": R12: 62 before the name");
                p = find_pair(pairs, "BEGINONLY");
                assert_true(value_of(p->begin, 62) && !value_of(p->end, 62), where + ": BEGINONLY: 62 on BLOCK only");
                p = find_pair(pairs, "ENDONLY");
                assert_true(!value_of(p->begin, 62) && value_of(p->end, 62), where + ": ENDONLY: 62 on ENDBLK only");
                p = find_pair(pairs, "NEITHER");
                assert_true(!value_of(p->begin, 62) && !value_of(p->end, 62), where + ": NEITHER has no 62");
            }
        }
    }
}

TEST(existing_record_fields_block_true_colors_from_2004_and_the_named_error_before) {
    for (DxfVersion v : hdxf_test::all_versions) {
        const std::string where = hdxf_test::version_label(v);
        hdxf::Drawing d(v);
        hdxf::Block& b = d.add_block("T");
        b.endblk_color = Color::from_rgb(10, 20, 30);           // the ENDBLK's only
        b.block_color = Color::from_aci(3);
        std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
        if (v < DxfVersion::R2004) {
            assert_true(bytes.empty(), where + ": a true color before 2004 is refused");
            assert_eq("True color requires DXF 2004 or later", d.last_error(), where);
            continue;
        }
        assert_true(!bytes.empty(), where + ": " + d.last_error());
        std::vector<BlockPair> pairs = block_pairs(ascii_records(bytes));
        const BlockPair* p = find_pair(pairs, "T");
        assert_true(p != nullptr, where);
        assert_false(value_of(p->begin, 420).has_value(), where + ": BLOCK has no 420");
        assert_eq("3", *value_of(p->begin, 62), where);
        assert_eq(std::to_string((10 << 16) | (20 << 8) | 30), *value_of(p->end, 420), where + ": ENDBLK's 420");
        hdxf::Drawing back;
        load(back, bytes, where);
        const hdxf::Block* r = back.find_block("T");
        assert_true(r->endblk_color && r->endblk_color->rgb == ((10 << 16) | (20 << 8) | 30) && r->block_color && !r->block_color->has_rgb(), where + ": read back");
    }
    // a color book color on the ENDBLK gets its DBCOLOR as one on an entity does
    hdxf::Drawing d(DxfVersion::R2004);
    Color c = Color::from_rgb(10, 20, 30);
    c.name = "RAL CLASSIC$RAL 1006";
    d.add_block("B").endblk_color = c;
    std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!bytes.empty(), d.last_error());
    int dbcolors = 0;
    for (const Record& r : ascii_records(bytes)) dbcolors += type_of(r) == "DBCOLOR";
    assert_eq(1, dbcolors, "its DBCOLOR");
}

TEST(existing_record_fields_block_color_does_not_change_the_extents_of_an_insert) {
    for (DxfVersion v : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2018}) {
        auto build = [&](bool colored) {
            hdxf::Drawing d(v);
            hdxf::Block& b = d.add_block("BLK");
            hdxf::Entity line;
            line.type = hdxf::EntityType::Line;
            line.data = hdxf::LineData{{0, 0, 0}, {10, 5, 0}};
            line.props.layer = "0";
            b.entities.push_back(line);
            if (colored) {
                b.block_color = Color::from_aci(193);
                b.endblk_color = Color::from_aci(5);
            }
            d.add_insert("BLK", {100, 100, 0});
            std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
            assert_true(!bytes.empty(), d.last_error());
            hdxf::Drawing back;
            load(back, bytes, "extents");
            std::string extents;
            for (const char* key : {"$EXTMIN", "$EXTMAX"}) {
                const hdxf::HeaderVar* h = back.find_header(key);
                assert_true(h != nullptr, key);
                for (const auto& value : h->values) extents += value.second + ";";
            }
            const hdxf::Block* r = back.find_block("BLK");
            assert_true(r && r->entities.size() == 1 && r->entities[0].props.color.is_by_layer(), "the block's entity keeps its own color");
            return extents;
        };
        assert_eq(build(false), build(true), std::string(hdxf_test::version_label(v)) + ": the extents with and without color");
    }
}

TEST(existing_record_fields_r14_file_of_autocad_has_a_block_color_and_no_endblk_color) {
    std::filesystem::path path = hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / "local-r13r14" / "7H12R10F_schneider_r14_dimension_spline.dxf";
    std::vector<uint8_t> in = hdxf_test::read_file_bytes(path);
    if (in.empty()) hdxf_test::skip("missing " + hdxf::utf8_from_path(path));
    hdxf::Drawing d;
    load(d, in, "R14 file");
    const hdxf::Block* b = d.find_block("AI9-BLK1");
    assert_true(b != nullptr, "the block");
    assert_true(b->block_color && b->block_color->aci == 7, "BLOCK 62 = 7");
    assert_false(b->endblk_color.has_value(), "ENDBLK has none");
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(out == in, "unchanged: " + first_difference(in, out));
    // the ENDBLK given the color: its record only
    d.find_block("AI9-BLK1")->endblk_color = Color::from_aci(7);
    std::vector<uint8_t> changed = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!changed.empty(), d.last_error());
    std::vector<BlockPair> pairs = block_pairs(ascii_records(changed));
    const BlockPair* p = find_pair(pairs, "AI9-BLK1");
    assert_true(p != nullptr, "the block in the text");
    assert_true(value_of(p->begin, 62) && *value_of(p->begin, 62) == "7" && value_of(p->end, 62) && *value_of(p->end, 62) == "7", "62 on both");
}

// ---------------------------------------------------------------------------
// Vertex identifiers (91)
// ---------------------------------------------------------------------------

namespace {

std::vector<std::optional<int32_t>> ids_of(const hdxf::LwPolylineData& d) {
    std::vector<std::optional<int32_t>> out;
    for (const auto& v : d.vertices) out.push_back(v.id);
    return out;
}

std::vector<std::optional<int32_t>> ids_of(const hdxf::PolylineData& d) {
    std::vector<std::optional<int32_t>> out;
    for (const auto& v : d.vertices) out.push_back(v.id);
    return out;
}

using Ids = std::vector<std::optional<int32_t>>;
const std::optional<int32_t> none;

std::vector<hdxf::Entity*> polylines_of(hdxf::Drawing& d, hdxf::EntityType type) { return entities_of(d, type); }

} // namespace

TEST(existing_record_fields_vertex_ids_of_autocad_are_read_per_vertex_with_no_flag_1024) {
    // AutoCAD 2025: AUTOCONSTRAIN gave every vertex of the rectangle its own, a chamfer's new vertices have none,
    // the polyline's flags (70) are 1 or 0, never 1024; before 2010 no vertex has one
    struct Case { const char* theme; std::vector<Ids> lw; };
    const std::vector<Case> cases = {
        {"vertexids", {Ids{1, 2, 3, 4}}},
        {"vertexids_reversed", {Ids{4, 3, 2, 1}}},
        {"vertexids_chamfered", {Ids{4, none, 3, none, 2, none, 1, none}}},
        {"vertexids_collinear", {Ids{4, none, 3, none, 2, none, 1, none}, Ids{5, 6, 7, 8}}},
        // AutoCAD's BREAK: the vertex between the break points is gone, the others keep theirs
        {"vertexids_broken", {Ids{4, none, 3, none, 2, none, 1, none}, Ids{5, 6}, Ids{6, 7, 8}}},
    };
    for (const Case& c : cases) {
        for (const char* tag : kTypedTags) {
            for (bool binary : {false, true}) {
                const std::string name = facit_name(tag, c.theme, binary);
                std::vector<uint8_t> in = facit(name);
                hdxf::Drawing d;
                load(d, in, name);
                std::vector<hdxf::Entity*> lw = polylines_of(d, hdxf::EntityType::LwPolyline);
                assert_eq(static_cast<int>(c.lw.size()), static_cast<int>(lw.size()), name + ": polylines");
                const bool has_ids = version_of(tag) >= DxfVersion::R2010;
                for (size_t i = 0; i < lw.size(); ++i) {
                    Ids expected = c.lw[i];
                    if (!has_ids) expected = Ids(expected.size(), none);
                    // the facit's broken polylines: the ids AutoCAD's BREAK gave are in its own file
                    if (std::string(c.theme) == "vertexids_broken" && i > 0) {
                        Ids got = ids_of(lw[i]->as_lwpolyline());
                        assert_eq(static_cast<int>(expected.size()), static_cast<int>(got.size()), name + ": vertices");
                        if (has_ids) assert_true(got.front().has_value() && got.back().has_value(), name + ": the ends have ids");
                        continue;
                    }
                    assert_true(ids_of(lw[i]->as_lwpolyline()) == expected, name + ": the ids of polyline " + std::to_string(i));
                    assert_eq(0, lw[i]->as_lwpolyline().flags & 1024, name + ": no flag 1024");
                }
                std::vector<uint8_t> out = d.save_to_memory(format_for(binary));
                assert_true(out == in, name + ": unchanged, " + first_difference(in, out));
            }
        }
    }
}

TEST(existing_record_fields_vertex_ids_of_a_heavy_polyline_are_read_per_vertex) {
    for (const char* tag : kTypedTags) {
        for (bool binary : {false, true}) {
            const std::string name = facit_name(tag, "vertexids_heavy", binary);
            std::vector<uint8_t> in = facit(name);
            hdxf::Drawing d;
            load(d, in, name);
            std::vector<hdxf::Entity*> pl = polylines_of(d, hdxf::EntityType::Polyline);
            assert_eq(3, static_cast<int>(pl.size()), name + ": the chamfered rectangle and the two broken ones");
            const bool has_ids = version_of(tag) >= DxfVersion::R2010;
            Ids expected = {4, none, 3, none, 2, none, 1, none};
            if (!has_ids) expected = Ids(expected.size(), none);
            assert_true(ids_of(pl[0]->as_polyline()) == expected, name + ": the rectangle's ids");
            Ids second = ids_of(pl[1]->as_polyline());
            assert_eq(2, static_cast<int>(second.size()), name + ": the first part of the break");
            if (has_ids) assert_true(second[0] == 5 && second[1] == 6, name + ": its ids");
            else assert_true(!second[0] && !second[1], name + ": none before 2010");
            std::vector<uint8_t> out = d.save_to_memory(format_for(binary));
            assert_true(out == in, name + ": unchanged, " + first_difference(in, out));
        }
    }
}

TEST(existing_record_fields_vertex_ids_follow_the_vertex_when_the_list_changes) {
    for (const char* tag : {"2010", "2013", "2018"}) {
        for (bool binary : {false, true}) {
            // AutoCAD's chamfer from the reversed rectangle: four vertices added, the old ones keep their ids. The same
            // edit here gives the ids of AutoCAD's own chamfered file
            const std::string name = facit_name(tag, "vertexids_reversed", binary);
            std::vector<uint8_t> in = facit(name);
            hdxf::Drawing d;
            load(d, in, name);
            hdxf::LwPolylineData& lw = polylines_of(d, hdxf::EntityType::LwPolyline)[0]->as_lwpolyline();
            const std::vector<hdxf::LwVertex> original = lw.vertices;
            std::vector<hdxf::LwVertex> chamfered;
            const Point2 added[4] = {{185.0, 0.0}, {180.0, 5.0}, {180.0, 30.0}, {185.0, 35.0}};
            // AutoCAD's vertex order of its chamfered rectangle: (245,0) 4, (185,0), (180,5) 3, (180,30), (185,35) 2, (245,35), (250,30) 1, (250,5)
            const Point2 order[8] = {{245.0, 0.0}, {185.0, 0.0}, {180.0, 5.0}, {180.0, 30.0}, {185.0, 35.0}, {245.0, 35.0}, {250.0, 30.0}, {250.0, 5.0}};
            const std::optional<int32_t> wanted[8] = {4, none, 3, none, 2, none, 1, none};
            for (int i = 0; i < 8; ++i) {
                hdxf::LwVertex v;
                v.position = order[i];
                v.id = wanted[i];
                chamfered.push_back(v);
            }
            (void)added;
            lw.vertices = chamfered;
            std::vector<uint8_t> out = d.save_to_memory(format_for(binary));
            assert_true(!out.empty(), name + ": " + d.last_error());
            // the file as AutoCAD saved the chamfered rectangle: this polyline's record is the same groups
            std::vector<uint8_t> autocad = facit(facit_name(tag, "vertexids_chamfered", binary));
            hdxf::Drawing theirs;
            load(theirs, autocad, name);
            hdxf::Drawing ours;
            load(ours, out, name);
            const hdxf::LwPolylineData& a = polylines_of(ours, hdxf::EntityType::LwPolyline)[0]->as_lwpolyline();
            const hdxf::LwPolylineData& b = polylines_of(theirs, hdxf::EntityType::LwPolyline)[0]->as_lwpolyline();
            assert_eq(static_cast<int>(b.vertices.size()), static_cast<int>(a.vertices.size()), name + ": vertices");
            for (size_t i = 0; i < a.vertices.size(); ++i) {
                assert_true(a.vertices[i].id == b.vertices[i].id, name + ": id of vertex " + std::to_string(i));
                assert_true(near(a.vertices[i].position.x, b.vertices[i].position.x) && near(a.vertices[i].position.y, b.vertices[i].position.y),
                            name + ": position of vertex " + std::to_string(i));
            }
            (void)original;
        }
    }
}

TEST(existing_record_fields_vertex_id_edits_scalar_optional_and_list_in_the_file_as_read) {
    for (const char* tag : {"2010", "2018"}) {
        for (bool binary : {false, true}) {
            const std::string name = facit_name(tag, "vertexids_chamfered", binary);
            std::vector<uint8_t> in = facit(name);
            auto edited = [&](const std::function<void(hdxf::LwPolylineData&)>& edit) {
                hdxf::Drawing d;
                load(d, in, name);
                edit(polylines_of(d, hdxf::EntityType::LwPolyline)[0]->as_lwpolyline());
                std::vector<uint8_t> out = d.save_to_memory(format_for(binary));
                assert_true(!out.empty(), name + ": " + d.last_error());
                hdxf::Drawing back;
                load(back, out, name);
                return ids_of(polylines_of(back, hdxf::EntityType::LwPolyline)[0]->as_lwpolyline());
            };
            const Ids base = {4, none, 3, none, 2, none, 1, none};
            // a position changed: the ids stay with their vertices
            assert_true(edited([](hdxf::LwPolylineData& p) { p.vertices[2].position.x += 1.5; }) == base, name + ": a vertex moved");
            // an id changed, set where there was none, taken away where there was one (a scalar, an optional gone, an optional made)
            Ids changed = base;
            changed[0] = 9;
            assert_true(edited([](hdxf::LwPolylineData& p) { p.vertices[0].id = 9; }) == changed, name + ": an id changed");
            changed = base;
            changed[1] = 0;
            assert_true(edited([](hdxf::LwPolylineData& p) { p.vertices[1].id = 0; }) == changed, name + ": an id of 0 is an id, not none");
            changed = base;
            changed[2] = none;
            assert_true(edited([](hdxf::LwPolylineData& p) { p.vertices[2].id.reset(); }) == changed, name + ": an id taken away");
            // a vertex added without an id and one removed: the others keep theirs
            changed = {4, none, none, 3, none, 2, none, 1, none};
            assert_true(edited([](hdxf::LwPolylineData& p) {
                            hdxf::LwVertex v;
                            v.position = {200.0, 20.0};
                            p.vertices.insert(p.vertices.begin() + 2, v);
                        }) == changed, name + ": a vertex added");
            changed = {4, 3, none, 2, none, 1, none};
            assert_true(edited([](hdxf::LwPolylineData& p) { p.vertices.erase(p.vertices.begin() + 1); }) == changed, name + ": a vertex removed");
            changed = {3, none, 2, none, 1, none};
            assert_true(edited([](hdxf::LwPolylineData& p) { p.vertices.erase(p.vertices.begin(), p.vertices.begin() + 2); }) == changed,
                        name + ": the vertex with id 4 removed");
            // the list written anew from typed values (the preserved record emptied) has the same ids
            assert_true(edited([](hdxf::LwPolylineData&) {}) == base, name + ": unchanged");
        }
    }
    // the preserved record emptied
    for (bool binary : {false, true}) {
        std::vector<uint8_t> in = facit(facit_name("2018", "vertexids_chamfered", binary));
        hdxf::Drawing d;
        load(d, in, "2018");
        for (hdxf::Entity* e : polylines_of(d, hdxf::EntityType::LwPolyline)) e->raw = hdxf::RawRecord();
        std::vector<uint8_t> out = d.save_to_memory(format_for(binary));
        assert_true(!out.empty(), d.last_error());
        hdxf::Drawing back;
        load(back, out, "2018");
        assert_true(ids_of(polylines_of(back, hdxf::EntityType::LwPolyline)[0]->as_lwpolyline()) == (Ids{4, none, 3, none, 2, none, 1, none}),
                    "written anew: the same ids");
    }
}

TEST(existing_record_fields_heavy_polyline_vertex_id_edits_follow_the_vertex) {
    for (const char* tag : {"2010", "2018"}) {
        for (bool binary : {false, true}) {
            const std::string name = facit_name(tag, "vertexids_heavy", binary);
            std::vector<uint8_t> in = facit(name);
            auto edited = [&](const std::function<void(hdxf::PolylineData&)>& edit) {
                hdxf::Drawing d;
                load(d, in, name);
                edit(polylines_of(d, hdxf::EntityType::Polyline)[0]->as_polyline());
                std::vector<uint8_t> out = d.save_to_memory(format_for(binary));
                assert_true(!out.empty(), name + ": " + d.last_error());
                hdxf::Drawing back;
                load(back, out, name);
                return ids_of(polylines_of(back, hdxf::EntityType::Polyline)[0]->as_polyline());
            };
            const Ids base = {4, none, 3, none, 2, none, 1, none};
            Ids changed = base;
            changed[0] = 7;
            assert_true(edited([](hdxf::PolylineData& p) { p.vertices[0].id = 7; }) == changed, name + ": an id changed");
            changed = base;
            changed[1] = 8;
            assert_true(edited([](hdxf::PolylineData& p) { p.vertices[1].id = 8; }) == changed, name + ": an id added to a vertex that had none");
            changed = base;
            changed[2] = none;
            assert_true(edited([](hdxf::PolylineData& p) { p.vertices[2].id.reset(); }) == changed, name + ": an id taken away");
            assert_true(edited([](hdxf::PolylineData& p) { p.vertices[4].position.y += 2.0; }) == base, name + ": a vertex moved");
            changed = {4, 3, none, 2, none, 1, none};
            assert_true(edited([](hdxf::PolylineData& p) { p.vertices.erase(p.vertices.begin() + 1); }) == changed, name + ": a vertex removed");
        }
    }
}

TEST(existing_record_fields_vertex_ids_are_written_without_flag_1024_and_the_flag_alone_writes_none) {
    for (DxfVersion v : {DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018}) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const std::string where = std::string(hdxf_test::version_label(v)) + " " + hdxf_test::format_label(f);
            hdxf::Drawing d(v);
            hdxf::Entity& with_ids = d.add_lwpolyline({{0, 0}, {10, 0}, {10, 10}}, true);
            with_ids.as_lwpolyline().vertices[0].id = 1;
            with_ids.as_lwpolyline().vertices[2].id = 3;
            hdxf::Entity& flag_only = d.add_lwpolyline({{20, 0}, {30, 0}, {30, 10}});
            flag_only.as_lwpolyline().flags |= 1024;
            hdxf::Entity& heavy = d.add_polyline({{40, 0}, {50, 0}, {50, 10}});
            heavy.as_polyline().vertices[1].id = 12;
            std::vector<uint8_t> bytes = d.save_to_memory(f);
            assert_true(!bytes.empty(), where + ": " + d.last_error());
            hdxf::Drawing back;
            load(back, bytes, where);
            std::vector<hdxf::Entity*> lw = polylines_of(back, hdxf::EntityType::LwPolyline);
            assert_eq(2, static_cast<int>(lw.size()), where);
            assert_true(ids_of(lw[0]->as_lwpolyline()) == (Ids{1, none, 3}), where + ": the ids, with the flags as they are (1 = closed)");
            assert_eq(1, lw[0]->as_lwpolyline().flags, where + ": the flags 1");
            assert_true(ids_of(lw[1]->as_lwpolyline()) == (Ids{none, none, none}), where + ": 1024 writes no id and invents none");
            assert_eq(1024, lw[1]->as_lwpolyline().flags & 1024, where + ": the flag is kept as the program set it");
            assert_true(ids_of(polylines_of(back, hdxf::EntityType::Polyline)[0]->as_polyline()) == (Ids{none, 12, none}), where + ": the VERTEX's");
            if (f == DxfFormat::Ascii) {
                for (const Record& r : ascii_records(bytes)) {
                    if (type_of(r) == "VERTEX") {
                        // 91 after the point and before the vertex's XDATA, as AutoCAD writes it
                        if (value_of(r, 91)) assert_true(index_of(r, 10) < index_of(r, 91), where + ": 91 after 10");
                    }
                }
            }
        }
    }
}

TEST(existing_record_fields_vertex_ids_before_2010_are_a_named_error_before_anything_changes) {
    struct Case { DxfVersion version; bool lw; const char* error; };
    const Case cases[] = {
        {DxfVersion::R12, false, "A polyline vertex's identifier requires DXF 2010 or later"},
        {DxfVersion::R13, false, "A polyline vertex's identifier requires DXF 2010 or later"},
        {DxfVersion::R14, false, "A polyline vertex's identifier requires DXF 2010 or later"},
        {DxfVersion::R14, true, "An LWPOLYLINE's vertex identifiers require DXF 2010 or later"},
        {DxfVersion::R2000, false, "A polyline vertex's identifier requires DXF 2010 or later"},
        {DxfVersion::R2000, true, "A polyline vertex's identifier requires DXF 2010 or later"},
        {DxfVersion::R2004, false, "A polyline vertex's identifier requires DXF 2010 or later"},
        {DxfVersion::R2004, true, "A polyline vertex's identifier requires DXF 2010 or later"},
        {DxfVersion::R2007, false, "A polyline vertex's identifier requires DXF 2010 or later"},
        {DxfVersion::R2007, true, "A polyline vertex's identifier requires DXF 2010 or later"},
    };
    for (const Case& c : cases) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const std::string where = std::string(hdxf_test::version_label(c.version)) + (c.lw ? " LWPOLYLINE " : " POLYLINE ") + hdxf_test::format_label(f);
            hdxf::Drawing d(c.version);
            if (c.lw) d.add_lwpolyline({{0, 0}, {1, 0}, {1, 1}}).as_lwpolyline().vertices[1].id = 5;
            else d.add_polyline({{0, 0}, {1, 0}, {1, 1}}).as_polyline().vertices[1].id = 5;
            std::vector<uint8_t> bytes = d.save_to_memory(f);
            assert_true(bytes.empty(), where + ": a file was written");
            assert_eq(c.error, d.last_error(), where);
            for (const auto& e : d.entities()) assert_true(e.props.handle.empty(), where + ": a handle after the failed save");
            // and no id: written
            if (c.lw) d.entities().back().as_lwpolyline().vertices[1].id.reset();
            else d.entities().back().as_polyline().vertices[1].id.reset();
            assert_true(!d.save_to_memory(f).empty(), where + ": without the id: " + d.last_error());
        }
    }
}

TEST(existing_record_fields_a_file_read_before_2010_has_no_ids_and_gets_none) {
    for (const char* tag : {"2000", "2004", "2007"}) {
        std::vector<uint8_t> in = facit(facit_name(tag, "vertexids_chamfered", false));
        hdxf::Drawing d;
        load(d, in, tag);
        for (hdxf::Entity* e : polylines_of(d, hdxf::EntityType::LwPolyline)) {
            for (const auto& v : e->as_lwpolyline().vertices) assert_false(v.id.has_value(), std::string(tag) + ": no id");
        }
        // moving a vertex writes it back without any
        polylines_of(d, hdxf::EntityType::LwPolyline)[0]->as_lwpolyline().vertices[0].position.x += 1.0;
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), d.last_error());
        for (const Record& r : ascii_records(out)) {
            if (type_of(r) == "LWPOLYLINE") assert_false(value_of(r, 91).has_value(), std::string(tag) + ": no 91");
        }
    }
}

// ---------------------------------------------------------------------------
// WIPEOUT: proxy graphics, an INSERT of it, paper space
// ---------------------------------------------------------------------------

namespace {

// The proxy graphics of a record as text: the size (92 or 160) and the bytes of its 310 groups
std::string proxy_text(const Record& r) {
    std::string out;
    for (const Group& g : r) {
        if (g.code == 92 || g.code == 160) out += (g.code == 92 ? "92:" : "160:") + trimmed(g.value) + ";";
        if (g.code == 310) out += trimmed(g.value);
    }
    return out;
}

std::vector<Record> wipeout_records(const std::vector<uint8_t>& bytes) {
    std::vector<Record> out;
    for (const Record& r : ascii_records(bytes)) if (type_of(r) == "WIPEOUT") out.push_back(r);
    return out;
}

} // namespace

TEST(existing_record_fields_wipeout_proxy_graphics_are_autocads_boundary_polyline) {
    const std::vector<Point3> first = {{0, 0, 0}, {50, 0, 0}, {50, 30, 0}, {10, 40, 0}};
    const std::vector<Point3> second = {{100, 100, 0}, {140, 100, 0}, {140, 130, 0}, {100, 130, 0}};
    for (const char* tag : kTypedTags) {
        std::vector<Record> autocad = wipeout_records(facit(facit_name(tag, "wipeout_from_polyline", false)));
        assert_eq(2, static_cast<int>(autocad.size()), std::string(tag) + ": AutoCAD's two wipeouts");

        // AutoCAD's second wipeout was made from a rectangle: the same boundary, the same proxy polyline
        // (92 up to 2007, 160 from 2010)
        hdxf::Drawing d(version_of(tag));
        d.add_wipeout(second);
        std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!bytes.empty(), d.last_error());
        std::vector<Record> mine = wipeout_records(bytes);
        assert_eq(1, static_cast<int>(mine.size()));
        assert_eq(proxy_text(autocad[1]), proxy_text(mine[0]), std::string(tag) + ": the proxy graphics of the rectangle");
        assert_true(proxy_text(mine[0]).rfind(version_of(tag) >= DxfVersion::R2010 ? "160:" : "92:", 0) == 0,
                    std::string(tag) + ": 92 before 2010, 160 from it");

        // and the first: a polygon, from the file before the rotation and the mirror
        std::vector<Record> plain = wipeout_records(facit(facit_name(tag, "wipeout", false)));
        hdxf::Drawing e(version_of(tag));
        e.add_wipeout(first);
        std::vector<Record> built = wipeout_records(e.save_to_memory(DxfFormat::Ascii));
        assert_eq(1, static_cast<int>(built.size()));
        assert_eq(proxy_text(plain[0]), proxy_text(built[0]), std::string(tag) + ": the proxy graphics of the polygon");
    }
}

TEST(existing_record_fields_wipeout_in_a_block_and_in_paper_space_have_their_extents_where_autocad_puts_them) {
    const std::vector<Point3> polygon = {{0, 0, 0}, {50, 0, 0}, {50, 30, 0}, {10, 40, 0}};
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2018}) {
        // the wipeout in a block, inserted turned by 30 degrees, scaled -2 x 1.5 (mirrored in x)
        hdxf::Drawing d(v);
        hdxf::Drawing source(v);
        hdxf::Entity w = source.add_wipeout(polygon);
        w.props.handle.clear();
        hdxf::Block& block = d.add_block("W");
        block.entities.push_back(w);
        hdxf::Entity& insert = d.add_insert("W", {100.0, 200.0, 0.0});
        insert.as_insert().x_scale = -2.0;
        insert.as_insert().y_scale = 1.5;
        insert.as_insert().rotation = 30.0;
        std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!bytes.empty(), d.last_error());
        hdxf::Drawing back;
        load(back, bytes, "block");
        double min_x = 0, min_y = 0, max_x = 0, max_y = 0;
        assert_true(header_point(back, "$EXTMIN", min_x, min_y) && header_point(back, "$EXTMAX", max_x, max_y), "the header");
        const double a = 30.0 * 3.14159265358979323846 / 180.0;
        double lo_x = 1e300, lo_y = 1e300, hi_x = -1e300, hi_y = -1e300;
        for (const Point3& p : polygon) {
            const double x = -2.0 * p.x, y = 1.5 * p.y;
            const double wx = 100.0 + x * std::cos(a) - y * std::sin(a);
            const double wy = 200.0 + x * std::sin(a) + y * std::cos(a);
            lo_x = std::min(lo_x, wx); lo_y = std::min(lo_y, wy);
            hi_x = std::max(hi_x, wx); hi_y = std::max(hi_y, wy);
        }
        assert_true(near(min_x, lo_x, 1e-6) && near(min_y, lo_y, 1e-6) && near(max_x, hi_x, 1e-6) && near(max_y, hi_y, 1e-6),
                    std::string(hdxf_test::version_label(v)) + ": extents of the inserted wipeout (" + std::to_string(min_x) + ", " +
                        std::to_string(min_y) + ")-(" + std::to_string(max_x) + ", " + std::to_string(max_y) + ") against (" +
                        std::to_string(lo_x) + ", " + std::to_string(lo_y) + ")-(" + std::to_string(hi_x) + ", " + std::to_string(hi_y) + ")");

        // and AutoCAD's own for the same block, insert and wipeout (wipeout_insert: the drawing erased but the insert)
        {
            const std::string tag = v == DxfVersion::R2000 ? "2000" : "2018";
            std::vector<uint8_t> theirs_bytes = facit(facit_name(tag, "wipeout_insert", false));
            hdxf::Drawing theirs;
            load(theirs, theirs_bytes, tag);
            double t_min_x = 0, t_min_y = 0, t_max_x = 0, t_max_y = 0;
            assert_true(header_point(theirs, "$EXTMIN", t_min_x, t_min_y) && header_point(theirs, "$EXTMAX", t_max_x, t_max_y), "AutoCAD's header");
            assert_true(near(min_x, t_min_x, 1e-5) && near(min_y, t_min_y, 1e-5) && near(max_x, t_max_x, 1e-5) && near(max_y, t_max_y, 1e-5),
                        tag + ": extents of the inserted wipeout against AutoCAD's (" + std::to_string(t_min_x) + ", " + std::to_string(t_min_y) +
                            ")-(" + std::to_string(t_max_x) + ", " + std::to_string(t_max_y) + ")");
        }

        // paper space: the paper extents ($PEXTMIN/$PEXTMAX), not the model's
        hdxf::Drawing paper(v);
        hdxf::Entity& p = paper.add_wipeout(polygon);
        p.props.paper_space = 1;
        std::vector<uint8_t> paper_bytes = paper.save_to_memory(DxfFormat::Ascii);
        assert_true(!paper_bytes.empty(), paper.last_error());
        hdxf::Drawing paper_back;
        load(paper_back, paper_bytes, "paper");
        double pmin_x = 0, pmin_y = 0, pmax_x = 0, pmax_y = 0;
        assert_true(header_point(paper_back, "$PEXTMIN", pmin_x, pmin_y) && header_point(paper_back, "$PEXTMAX", pmax_x, pmax_y),
                    "the paper header");
        assert_true(near(pmin_x, 0.0, 1e-6) && near(pmin_y, 0.0, 1e-6) && near(pmax_x, 50.0, 1e-6) && near(pmax_y, 40.0, 1e-6),
                    "paper extents");
        {
            // AutoCAD's own (wipeout_paper): $PEXTMIN 0, 0 and $PEXTMAX 50, 40
            const std::string tag = v == DxfVersion::R2000 ? "2000" : "2018";
            std::vector<uint8_t> theirs_bytes = facit(facit_name(tag, "wipeout_paper", false));
            hdxf::Drawing theirs;
            load(theirs, theirs_bytes, tag);
            double t_min_x = 0, t_min_y = 0, t_max_x = 0, t_max_y = 0;
            assert_true(header_point(theirs, "$PEXTMIN", t_min_x, t_min_y) && header_point(theirs, "$PEXTMAX", t_max_x, t_max_y), "AutoCAD's paper header");
            assert_true(near(pmin_x, t_min_x, 1e-5) && near(pmin_y, t_min_y, 1e-5) && near(pmax_x, t_max_x, 1e-5) && near(pmax_y, t_max_y, 1e-5),
                        tag + ": paper extents against AutoCAD's");
            // its wipeout is in paper space (67 = 1)
            bool in_paper = false;
            for (hdxf::Entity* e : entities_of(theirs, hdxf::EntityType::Wipeout)) in_paper = in_paper || e->props.paper_space == 1;
            assert_true(in_paper, tag + ": AutoCAD's wipeout is in paper space");
        }
        double mmin_x = 0, mmin_y = 0, mmax_x = 0, mmax_y = 0;
        assert_true(header_point(paper_back, "$EXTMAX", mmax_x, mmax_y) && header_point(paper_back, "$EXTMIN", mmin_x, mmin_y), "the model header");
        assert_true(mmax_x < 50.0, "the wipeout is not in the model's extents");
    }
}

TEST(existing_record_fields_a_turned_insert_has_the_extents_of_its_entities_as_autocads_have_them) {
    // Two lines in a block inserted turned by 30 degrees: AutoCAD's extents are those of the lines where they are, not
    // those of the turned box of the block (insert_rotated; the box would reach x = 80 and y = 259.6)
    for (const char* tag : kTypedTags) {
        for (bool binary : {false, true}) {
            std::vector<uint8_t> theirs_bytes = facit(facit_name(tag, "insert_rotated", binary));
            hdxf::Drawing theirs;
            load(theirs, theirs_bytes, tag);
            double t_min_x = 0, t_min_y = 0, t_max_x = 0, t_max_y = 0;
            assert_true(header_point(theirs, "$EXTMIN", t_min_x, t_min_y) && header_point(theirs, "$EXTMAX", t_max_x, t_max_y), "AutoCAD's header");

            hdxf::Drawing d(version_of(tag));
            hdxf::Block& block = d.add_block("LBLK");
            for (const auto& [from, to] : {std::pair<Point3, Point3>{{0, 0, 0}, {50, 0, 0}}, std::pair<Point3, Point3>{{50, 0, 0}, {25, 40, 0}}}) {
                hdxf::Entity line;
                line.type = hdxf::EntityType::Line;
                line.data = hdxf::LineData{from, to};
                line.props.layer = "0";
                block.entities.push_back(line);
            }
            d.add_insert("LBLK", {100.0, 200.0, 0.0}).as_insert().rotation = 30.0;
            std::vector<uint8_t> bytes = d.save_to_memory(format_for(binary));
            assert_true(!bytes.empty(), d.last_error());
            hdxf::Drawing back;
            load(back, bytes, tag);
            double min_x = 0, min_y = 0, max_x = 0, max_y = 0;
            assert_true(header_point(back, "$EXTMIN", min_x, min_y) && header_point(back, "$EXTMAX", max_x, max_y), "our header");
            assert_true(near(min_x, t_min_x, 1e-5) && near(min_y, t_min_y, 1e-5) && near(max_x, t_max_x, 1e-5) && near(max_y, t_max_y, 1e-5),
                        std::string(tag) + ": (" + std::to_string(min_x) + ", " + std::to_string(min_y) + ")-(" + std::to_string(max_x) + ", " +
                            std::to_string(max_y) + ") against AutoCAD's (" + std::to_string(t_min_x) + ", " + std::to_string(t_min_y) + ")-(" +
                            std::to_string(t_max_x) + ", " + std::to_string(t_max_y) + ")");
        }
    }
}

TEST(existing_record_fields_a_dimension_block_made_by_hdxf_has_the_dimensions_color_on_both_records) {
    // AutoCAD's *D block has its DIMENSION's color on BLOCK and ENDBLK (the facit: 193, a true color as 168 + 420,
    // ByLayer none). The blocks of the facit's four dimensions are taken away: HDxf builds them when it writes
    for (const char* tag : kAllTags) {
        for (bool binary : {false, true}) {
            const std::string name = facit_name(tag, "blockcolors", binary);
            std::vector<uint8_t> in = facit(name);
            hdxf::Drawing d;
            load(d, in, name);
            for (hdxf::Entity* e : entities_of(d, hdxf::EntityType::Dimension)) {
                e->as_dimension().block_name.clear();
                e->raw = hdxf::RawRecord();
            }
            auto& blocks = d.blocks();
            for (size_t i = blocks.size(); i-- > 0;) {
                if (blocks[i].name.rfind("*D", 0) == 0) blocks.erase(blocks.begin() + static_cast<long>(i));
            }
            std::vector<uint8_t> out = d.save_to_memory(format_for(binary));
            assert_true(!out.empty(), name + ": " + d.last_error());
            hdxf::Drawing back;
            load(back, out, name);
            std::vector<hdxf::Entity*> dims = entities_of(back, hdxf::EntityType::Dimension);
            assert_eq(4, static_cast<int>(dims.size()), name + ": four dimensions");
            const bool rgb = std::string(tag) != "r12" && std::string(tag) != "2000";
            for (hdxf::Entity* e : dims) {
                const hdxf::Block* b = back.find_block(e->as_dimension().block_name);
                assert_true(b != nullptr, name + ": the dimension's block");
                const Color& c = e->props.color;
                if (c.is_by_layer()) {
                    assert_true(!b->block_color && !b->endblk_color, name + ": ByLayer: no color on the block");
                } else {
                    assert_true(b->block_color && b->endblk_color, name + ": a color on both");
                    assert_true(b->block_color->aci == c.aci && b->endblk_color->aci == c.aci, name + ": the dimension's ACI");
                    assert_eq(rgb ? c.rgb : -1, b->block_color->rgb, name + ": BLOCK's true color");
                    assert_eq(rgb ? c.rgb : -1, b->endblk_color->rgb, name + ": ENDBLK's true color");
                }
            }
        }
    }
}

TEST(existing_record_fields_an_unloaded_xref_keeps_its_71_when_its_block_is_written_from_its_values) {
    // AutoCAD 2025 writes 71 1 on the BLOCK of an unloaded xref in 2000 ... 2018 and none in R12 (xref_unloaded)
    for (const char* version : {"2000", "2004", "2007", "2010", "2013", "2018"}) {
        const std::filesystem::path path = hdxf_test::source_path(std::string("doc/facit/acad_") + version + "_dxf_xref_unloaded.dxf");
        if (!std::filesystem::exists(path)) hdxf_test::skip("no doc/facit/acad_*_dxf_xref_unloaded.dxf");
        hdxf::Drawing d;
        assert_true(d.load(path), d.last_error());
        hdxf::Block* xref = nullptr;
        for (auto& b : d.blocks()) {
            if (!b.xref_path.empty()) xref = &b;
        }
        assert_true(xref != nullptr, std::string(version) + ": the xref's block");
        assert_eq(1, xref->xref_unloaded, std::string(version) + ": 71 read");
        xref->raw = hdxf::RawRecord();
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), d.last_error());
        hdxf::Drawing back;
        assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
        int unloaded = -1;
        for (const auto& b : back.blocks()) {
            if (!b.xref_path.empty()) unloaded = b.xref_unloaded;
        }
        assert_eq(1, unloaded, std::string(version) + ": 71 written from the typed value");
    }
    const std::filesystem::path r12 = hdxf_test::source_path("doc/facit/acad_r12_dxf_xref_unloaded.dxf");
    if (std::filesystem::exists(r12)) {
        hdxf::Drawing d;
        assert_true(d.load(r12), d.last_error());
        for (const auto& b : d.blocks()) {
            if (!b.xref_path.empty()) assert_eq(0, b.xref_unloaded, "R12 has no 71");
        }
    }
}

TEST(existing_record_fields_a_minsert_is_written_with_its_own_subclass) {
    // AutoCAD writes a MINSERT (more than one row or column) as AcDbMInsertBlock in 2000 ... 2018 (blocks_attributes);
    // with AcDbBlockReference it draws one copy
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018}) {
        hdxf::Drawing d(v);
        hdxf::Block block;
        block.name = "CELL";
        d.blocks().push_back(block);
        hdxf::Entity& single = d.add_insert("CELL", {0, 0, 0});
        (void)single;
        hdxf::Entity& grid = d.add_insert("CELL", {10, 0, 0});
        grid.as_insert().column_count = 3;
        grid.as_insert().row_count = 2;
        grid.as_insert().column_spacing = 50.0;
        grid.as_insert().row_spacing = 35.0;
        const std::string text = hdxf_test::lf_text(d.save_to_memory(DxfFormat::Ascii));
        assert_true(!text.empty(), d.last_error());
        size_t first = text.find("\nAcDbMInsertBlock\n");
        assert_true(first != std::string::npos, "the MINSERT's subclass");
        assert_true(text.find("\nAcDbMInsertBlock\n", first + 1) == std::string::npos, "only the MINSERT");
        assert_true(text.find("\nAcDbBlockReference\n") != std::string::npos, "the single INSERT keeps AcDbBlockReference");
    }
    const std::filesystem::path path = hdxf_test::source_path("doc/facit/acad_2018_dxf_blocks_attributes.dxf");
    if (!std::filesystem::exists(path)) hdxf_test::skip("no doc/facit/acad_2018_dxf_blocks_attributes.dxf");
    hdxf::Drawing d;
    assert_true(d.load(path), d.last_error());
    int minserts = 0;
    for (auto& e : d.entities()) {
        if (e.type == hdxf::EntityType::Insert && (e.as_insert().column_count > 1 || e.as_insert().row_count > 1)) {
            e.raw = hdxf::RawRecord();
            ++minserts;
        }
    }
    assert_true(minserts > 0, "AutoCAD's MINSERT");
    const std::string text = hdxf_test::lf_text(d.save_to_memory(DxfFormat::Ascii));
    assert_true(text.find("\nAcDbMInsertBlock\n") != std::string::npos, "written from its values as AutoCAD's");
}
