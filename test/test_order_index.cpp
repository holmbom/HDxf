#include "test_framework.hpp"
#include "type_version_matrix.hpp"
#include "typed_corpus.hpp"
#include "hdxf.hpp"
#include "TypedRecordTypes.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Step 9.4 of phase 9 (plan 2026-10-27-fas-9-typer.md): draw order, clipping and indexes — SORTENTSTABLE,
// SPATIAL_FILTER, LAYER_INDEX, SPATIAL_INDEX, IDBUFFER and LAYER_FILTER.
//
// The AutoCAD originals are doc/facit/acad_<version>_dxf_order_index*.dxf (made by doc/facit/fas9_order_index.scr with
// AutoCAD 2025's core: three INSERTs with an XCLIP each, draw order in the model space and Layout1, INDEXCTL 3 and a
// DWG save, then one change per file), doc/facit/acad_<version>_dxf_native_records.dxf (LAYER_FILTER, made in the
// GUI by 21_native_records.scr) and ACadSharp's sample_AC1015 (build/test-data). They are local files (the facit is
// not in git): a test whose file is missing is skipped.
// ---------------------------------------------------------------------------

using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::Object;
using hdxf::ObjectType;
using hdxf::Point2;
using hdxf::Point3;
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

std::string order_index_file(const char* tag, bool binary, const std::string& state = "") {
    return std::string("acad_") + tag + "_dxf_order_index" + (state.empty() ? "" : "_" + state) + (binary ? "_bin" : "") + ".dxf";
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

std::vector<const Object*> objects_of(const Drawing& d, ObjectType type) {
    std::vector<const Object*> out;
    for (const Object& o : d.objects()) if (o.type == type) out.push_back(&o);
    return out;
}

std::vector<Object*> objects_mut(Drawing& d, ObjectType type) {
    std::vector<Object*> out;
    for (Object& o : d.objects()) if (o.type == type) out.push_back(&o);
    return out;
}

DxfFormat format_of(const std::vector<uint8_t>& bytes) {
    return bytes.size() > 18 && std::string(bytes.begin(), bytes.begin() + 18) == "AutoCAD Binary DXF" ? DxfFormat::Binary : DxfFormat::Ascii;
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

std::string trim(std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

// The groups of a record after its subclass marker `subclass` and up to the next marker
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

// What a record holds as one text, to compare two readings of the same drawing (the typed values in file order)
std::string describe(const Object& o) {
    std::ostringstream s;
    s.precision(17);
    switch (o.type) {
        case ObjectType::SortentsTable: {
            const auto& d = o.as_sortents_table();
            s << "SORTENTS " << d.block_record_handle;
            for (const auto& e : d.entries) s << " (" << e.entity_handle << " " << e.sort_key << ")";
            break;
        }
        case ObjectType::SpatialFilter: {
            const auto& d = o.as_spatial_filter();
            s << "FILTER " << d.boundary.size();
            for (const auto& p : d.boundary) s << " " << p.x << "," << p.y;
            s << " n" << d.normal.x << "," << d.normal.y << "," << d.normal.z << " o" << d.origin.x << "," << d.origin.y << "," << d.origin.z
              << " e" << d.enabled << " f" << (d.front_distance ? std::to_string(*d.front_distance) : "-") << " b"
              << (d.back_distance ? std::to_string(*d.back_distance) : "-") << " i";
            for (double v : d.inverse_insert_transform) s << v << ",";
            s << " c";
            for (double v : d.clip_transform) s << v << ",";
            break;
        }
        case ObjectType::LayerIndex: {
            const auto& d = o.as_layer_index();
            s << "LAYERINDEX " << d.timestamp;
            for (const auto& e : d.entries) s << " (" << e.layer_name << " " << e.idbuffer_handle << " " << e.count << ")";
            break;
        }
        case ObjectType::SpatialIndex: {
            const auto& d = o.as_spatial_index();
            s << "SPATIALINDEX " << d.timestamp;
            if (d.tree) {
                s << " tree";
                for (double v : d.tree->values) s << " " << v;
                s << " " << d.tree->reserved << " " << d.tree->data.size() << " " << d.tree->end_marker << " ";
                for (uint8_t b : d.tree->data) s << std::hex << static_cast<int>(b) << std::dec << ".";
            }
            break;
        }
        case ObjectType::IdBuffer: {
            s << "IDBUFFER";
            for (const auto& h : o.as_id_buffer().object_handles) s << " " << h;
            break;
        }
        case ObjectType::LayerFilter: {
            s << "LAYERFILTER";
            for (const auto& l : o.as_layer_filter().layers) s << " " << l;
            break;
        }
        default:
            break;
    }
    return s.str();
}

bool is_order_index_type(ObjectType t) {
    return t == ObjectType::SortentsTable || t == ObjectType::SpatialFilter || t == ObjectType::LayerIndex ||
           t == ObjectType::SpatialIndex || t == ObjectType::IdBuffer || t == ObjectType::LayerFilter;
}

std::vector<std::string> describe_all(const Drawing& d) {
    std::vector<std::string> out;
    for (const Object& o : d.objects()) if (is_order_index_type(o.type)) out.push_back(o.handle + ": " + describe(o));
    return out;
}

bool same_groups(const Object& a, const Object& b) {
    std::vector<hdxf::DxfGroup> x = a.raw.groups(), y = b.raw.groups();
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); ++i) if (x[i].code != y[i].code || x[i].value != y[i].value) return false;
    return true;
}

// Every object but those of `except` (handles) is the record it was
void expect_other_records_as_they_were(const Drawing& before, const Drawing& after, const std::set<std::string>& except,
                                       const std::string& what) {
    for (const Object& o : before.objects()) {
        if (except.count(o.handle)) continue;
        const Object* now = after.find_object(o.handle);
        assert_true(now != nullptr, what + ": " + o.handle + " is gone");
        assert_true(same_groups(o, *now), what + ": " + o.handle + " changed");
    }
}

const Object* find_by_type_and_index(const Drawing& d, ObjectType type, size_t index) {
    std::vector<const Object*> list = objects_of(d, type);
    return index < list.size() ? list[index] : nullptr;
}

// A line of the model space with a handle of our own (to point at)
hdxf::Entity& line_with_handle(Drawing& d, const std::string& handle, double x = 0.0) {
    hdxf::Entity& e = d.add_line({x, 0, 0}, {x + 1, 1, 0});
    e.props.handle = handle;
    return e;
}

Object make_object(ObjectType type, const std::string& handle, hdxf::ObjectData data) {
    Object o;
    o.type = type;
    o.handle = handle;
    o.owner = "0";
    o.data = std::move(data);
    return o;
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
std::string drawing_with_objects(const std::vector<Rec>& records) {
    Drawing d(DxfVersion::R2000);
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

Rec sortents(const std::string& handle, const std::vector<Group>& body) {
    Rec r = {{0, "SORTENTSTABLE"}, {5, handle}, {330, "0"}, {100, "AcDbSortentsTable"}};
    r.insert(r.end(), body.begin(), body.end());
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// Reading AutoCAD's own files
// ---------------------------------------------------------------------------

TEST(order_index_reads_autocad_files_group_by_group_and_writes_them_back_unchanged) {
    for (const char* tag : kTags) {
        std::vector<uint8_t> ascii_in = facit_bytes(order_index_file(tag, false));
        std::vector<uint8_t> binary_in = facit_bytes(order_index_file(tag, true));
        Drawing ascii = load(ascii_in, tag);
        Drawing binary = load(binary_in, std::string(tag) + " binary");
        std::vector<Rec> all = ascii_records(ascii_in);

        // The typed values are the groups of the file: SORTENTSTABLE (331 5)*, with the block record (330)
        std::vector<Rec> sort_recs = records_named(all, "SORTENTSTABLE");
        std::vector<const Object*> sorts = objects_of(ascii, ObjectType::SortentsTable);
        assert_eq(2, static_cast<int>(sorts.size()), std::string(tag) + ": two draw orders");
        assert_eq(2, static_cast<int>(sort_recs.size()), "the file's two");
        for (size_t i = 0; i < sorts.size(); ++i) {
            std::vector<Group> body = after_marker(sort_recs[i], "AcDbSortentsTable");
            assert_true(!body.empty() && body[0].first == 330, "330 first in the subclass");
            assert_eq(trim(body[0].second), sorts[i]->as_sortents_table().block_record_handle, "the block record");
            const auto& entries = sorts[i]->as_sortents_table().entries;
            assert_eq(static_cast<int>((body.size() - 1) / 2), static_cast<int>(entries.size()), std::string(tag) + ": the pairs");
            for (size_t k = 0; k < entries.size(); ++k) {
                assert_eq(body[1 + 2 * k].first, 331, "331 then 5");
                assert_eq(body[2 + 2 * k].first, 5, "331 then 5");
                assert_eq(trim(body[1 + 2 * k].second), entries[k].entity_handle, "the entity");
                assert_eq(trim(body[2 + 2 * k].second), entries[k].sort_key, "the sort handle");
                assert_true(ascii.find_entity(entries[k].entity_handle) != nullptr, "the entity is in the drawing");
            }
        }
        assert_eq(7, static_cast<int>(sorts[0]->as_sortents_table().entries.size()), "the model space's draw order");
        assert_eq(4, static_cast<int>(sorts[1]->as_sortents_table().entries.size()), "Layout1's draw order");
        const hdxf::Block* model = ascii.find_block("*Model_Space");
        assert_true(model != nullptr, "model space");
        assert_eq(model->record.handle, sorts[0]->as_sortents_table().block_record_handle, "the model space's block record");

        // SPATIAL_FILTER: a rectangle, a polygon with both clip planes, an inverted rectangle
        std::vector<const Object*> filters = objects_of(ascii, ObjectType::SpatialFilter);
        assert_eq(3, static_cast<int>(filters.size()), std::string(tag) + ": three clips");
        const auto& rect = filters[0]->as_spatial_filter();
        assert_eq(2, static_cast<int>(rect.boundary.size()), "a rectangle");
        assert_eq(105.0, rect.boundary[0].x);
        assert_eq(5.0, rect.boundary[0].y);
        assert_eq(150.0, rect.boundary[1].x);
        assert_eq(50.0, rect.boundary[1].y);
        assert_true(rect.enabled && !rect.front_distance && !rect.back_distance, "no clip planes");
        assert_eq(-100.0, rect.inverse_insert_transform[3], "the inverse of the INSERT at x 100");
        assert_eq(1.0, rect.inverse_insert_transform[0]);
        assert_eq(1.0, rect.clip_transform[5]);
        const auto& polygon = filters[1]->as_spatial_filter();
        assert_eq(4, static_cast<int>(polygon.boundary.size()), "a polygon");
        assert_true(polygon.front_distance && *polygon.front_distance == 60.0, "the front plane (72, 40)");
        assert_true(polygon.back_distance && *polygon.back_distance == -5.0, "the back plane (73, 41)");
        assert_eq(-100.0, polygon.inverse_insert_transform[7], "the INSERT at y 100");
        const auto& inverted = filters[2]->as_spatial_filter();
        assert_eq(11, static_cast<int>(inverted.boundary.size()), "an inverted clip's polygon");
        assert_eq(-200.0, inverted.inverse_insert_transform[7], "the INSERT at y 200");
        assert_false(filters[2]->xdictionary.empty(), "the inverted clip has an extension dictionary (its round trip XRECORD)");
        assert_true(filters[0]->xdictionary.empty() && filters[1]->xdictionary.empty(), "the others have none");

        // LAYER_INDEX: (8 360 90)* and the IDBUFFERs it owns, with the same number of members
        std::vector<const Object*> indexes = objects_of(ascii, ObjectType::LayerIndex);
        assert_eq(1, static_cast<int>(indexes.size()), "a layer index");
        const auto& index = indexes[0]->as_layer_index();
        std::vector<Rec> index_recs = records_named(all, "LAYER_INDEX");
        std::vector<Group> index_body = after_marker(index_recs[0], "AcDbLayerIndex");
        assert_eq(static_cast<int>(index_body.size() / 3), static_cast<int>(index.entries.size()), "the entries");
        for (size_t k = 0; k < index.entries.size(); ++k) {
            assert_eq(trim(index_body[3 * k].second), index.entries[k].layer_name, "the layer");
            assert_eq(trim(index_body[3 * k + 1].second), index.entries[k].idbuffer_handle, "the IDBUFFER");
            assert_eq(std::atoi(index_body[3 * k + 2].second.c_str()), index.entries[k].count, "the count");
            const Object* buffer = ascii.find_object(index.entries[k].idbuffer_handle);
            assert_true(buffer && buffer->type == ObjectType::IdBuffer, "the IDBUFFER is there");
            assert_eq(index.entries[k].count, static_cast<int>(buffer->as_id_buffer().object_handles.size()),
                      std::string(tag) + ": " + index.entries[k].layer_name + "'s count is its IDBUFFER's members");
            for (const std::string& h : buffer->as_id_buffer().object_handles) {
                assert_true(ascii.find_entity(h) != nullptr, "an IDBUFFER member is an entity of the drawing");
            }
        }
        assert_true(index.timestamp > 2400000.0 && index.timestamp < 2600000.0, "a Julian date");
        assert_eq(4, static_cast<int>(index.entries.size()), "0, HDXF_A, HDXF_B, HDXF_C");
        assert_eq(std::string("HDXF_A"), index.entries[1].layer_name);
        assert_eq(3, index.entries[1].count, "the three INSERTs on HDXF_A");
        assert_eq(4, index.entries[2].count, "four entities on HDXF_B");
        assert_eq(0, index.entries[0].count, "an empty IDBUFFER is as valid as a full one");

        // SPATIAL_INDEX: the timestamp (AcDbIndex), six numbers, the length, the tree (310) and the end marker (1)
        std::vector<const Object*> spatial = objects_of(ascii, ObjectType::SpatialIndex);
        assert_eq(1, static_cast<int>(spatial.size()), "a spatial index");
        const auto& si = spatial[0]->as_spatial_index();
        assert_eq(index.timestamp, si.timestamp, "the same timestamp as the layer index");
        assert_true(si.tree.has_value(), "AutoCAD's own file has the tree");
        std::vector<Rec> spatial_recs = records_named(all, "SPATIAL_INDEX");
        std::vector<Group> si_body = after_marker(spatial_recs[0], "AcDbSpatialIndex");
        std::string hex;
        int length = 0;
        for (const Group& g : si_body) {
            if (g.first == 310) hex += trim(g.second);
            if (g.first == 90) length = std::atoi(g.second.c_str());
        }
        assert_eq(length, static_cast<int>(si.tree->data.size()), "the length (the second 90) is the tree's");
        assert_eq(static_cast<int>(hex.size() / 2), static_cast<int>(si.tree->data.size()), "the bytes of the 310 groups");
        assert_eq(std::string("END ACDBSPATIALINDEX BINARY DATA"), si.tree->end_marker);
        assert_eq(0, si.tree->reserved, "the first 90");

        // IDBUFFERs: members are pointers
        assert_eq(4, static_cast<int>(objects_of(ascii, ObjectType::IdBuffer).size()), "four IDBUFFERs");

        // The binary twin is the same drawing
        assert_true(describe_all(ascii).size() == describe_all(binary).size(), "the same number of records in the twin");
        std::vector<std::string> a = describe_all(ascii), b = describe_all(binary);
        for (size_t i = 0; i < a.size(); ++i) {
            // (ASCII has 16 digits, binary the whole double: compare what is exactly the same text, and the structure of the rest)
            assert_eq(a[i].substr(0, a[i].find(' ')), b[i].substr(0, b[i].find(' ')), "the twin's records are the same ones");
        }

        // B12: nothing changed, the same bytes, in both formats
        std::vector<uint8_t> out = save(ascii, DxfFormat::Ascii, tag);
        assert_true(out == ascii_in, std::string(tag) + ": ASCII written unchanged is the file");
        std::vector<uint8_t> out_binary = save(binary, DxfFormat::Binary, tag);
        assert_true(out_binary == binary_in, std::string(tag) + ": binary written unchanged is the file");
        assert_true(ascii.warnings().empty() || std::none_of(ascii.warnings().begin(), ascii.warnings().end(),
                    [](const std::string& w) { return w.find("kept as it was read") != std::string::npos; }), "no record was left untyped");
    }
}

TEST(order_index_every_state_of_the_facit_reads_and_writes_back_unchanged) {
    const char* states[] = {"layer_changed", "erased", "draworder_changed", "clip_deleted", "layer_renamed", "layer_purged"};
    for (const char* tag : {"2000", "2013", "2018"}) {
        for (const char* state : states) {
            for (bool binary : {false, true}) {
                std::vector<uint8_t> in = facit_bytes(order_index_file(tag, binary, state));
                Drawing d = load(in, state);
                for (const Object& o : d.objects()) {
                    if (is_order_index_type(o.type)) assert_false(o.raw.empty(), "the preserved record");
                    assert_false(o.type == ObjectType::Unknown && (o.as_unknown().object_type == "SORTENTSTABLE" ||
                                 o.as_unknown().object_type == "SPATIAL_FILTER" || o.as_unknown().object_type == "LAYER_INDEX" ||
                                 o.as_unknown().object_type == "SPATIAL_INDEX" || o.as_unknown().object_type == "IDBUFFER"),
                                 std::string(state) + ": a record of the family was left untyped");
                }
                std::vector<uint8_t> out = save(d, binary ? DxfFormat::Binary : DxfFormat::Ascii, state);
                assert_true(out == in, std::string(tag) + " " + state + (binary ? " binary" : "") + ": written unchanged");
            }
        }
    }
}

TEST(order_index_autocads_own_changes_are_what_the_records_say) {
    // The facit pairs: one change at a time (doc/facit/fas9_order_index.scr)
    for (const char* tag : {"2000", "2018"}) {
        Drawing base = load(facit_bytes(order_index_file(tag, false)), tag);
        Drawing layer_changed = load(facit_bytes(order_index_file(tag, false, "layer_changed")), "layer_changed");
        Drawing erased = load(facit_bytes(order_index_file(tag, false, "erased")), "erased");
        Drawing draw_order = load(facit_bytes(order_index_file(tag, false, "draworder_changed")), "draworder_changed");
        Drawing clip_deleted = load(facit_bytes(order_index_file(tag, false, "clip_deleted")), "clip_deleted");
        Drawing renamed = load(facit_bytes(order_index_file(tag, false, "layer_renamed")), "layer_renamed");
        Drawing purged = load(facit_bytes(order_index_file(tag, false, "layer_purged")), "layer_purged");

        // A circle moved to HDXF_B: the layer's IDBUFFER has the new member and the index counts it; the timestamp moves
        auto count_of = [](const Drawing& d, const std::string& layer) {
            for (const Object* o : objects_of(d, ObjectType::LayerIndex)) {
                for (const auto& e : o->as_layer_index().entries) if (e.layer_name == layer) return e.count;
            }
            return -1;
        };
        assert_eq(4, count_of(base, "HDXF_B"));
        assert_eq(5, count_of(layer_changed, "HDXF_B"), "the entity on HDXF_B is in the index");
        assert_eq(4, count_of(erased, "HDXF_B"), "erased again");
        // (AutoCAD's timestamp does not go back; it moves when the index was rebuilt, not at every save)
        assert_true(objects_of(layer_changed, ObjectType::LayerIndex)[0]->as_layer_index().timestamp >=
                    objects_of(base, ObjectType::LayerIndex)[0]->as_layer_index().timestamp, "the timestamp does not go back");
        assert_true(objects_of(erased, ObjectType::LayerIndex)[0]->as_layer_index().timestamp >
                    objects_of(base, ObjectType::LayerIndex)[0]->as_layer_index().timestamp, "and moves with a later rebuild");

        // The draw order after DRAWORDER: the same pairs, two keys exchanged
        const auto& before = objects_of(erased, ObjectType::SortentsTable)[0]->as_sortents_table().entries;
        const auto& after = objects_of(draw_order, ObjectType::SortentsTable)[0]->as_sortents_table().entries;
        // (an entity DRAWORDER moves that the table did not list gets a pair of its own: one more at most)
        assert_true(after.size() >= before.size() && after.size() <= before.size() + 1, "the pairs: " + std::to_string(after.size()));
        int moved = 0;
        for (size_t i = 0; i < before.size(); ++i) {
            assert_eq(before[i].entity_handle, after[i].entity_handle, "the entities stay in place");
            if (before[i].sort_key != after[i].sort_key) ++moved;
        }
        assert_true(moved >= 2, "the keys of at least two entities changed (DRAWORDER)");

        // XCLIP Delete: the clip of the INSERT is gone
        assert_eq(3, static_cast<int>(objects_of(draw_order, ObjectType::SpatialFilter).size()));
        assert_eq(2, static_cast<int>(objects_of(clip_deleted, ObjectType::SpatialFilter).size()));

        // The layer renamed: the index follows by name (AutoCAD moves the entry last), and a purged layer's entry stays
        assert_eq(-1, count_of(renamed, "HDXF_A"));
        assert_true(count_of(renamed, "HDXF_AR") >= 0, "the entry has the new name");
        assert_eq(4, static_cast<int>(objects_of(purged, ObjectType::LayerIndex)[0]->as_layer_index().entries.size()),
                  "a purged layer is still in the index (AutoCAD keeps its entry, with 0 members)");
        assert_eq(0, count_of(purged, "HDXF_C"));
    }
}

TEST(order_index_layer_filter_from_autocads_native_records_files) {
    for (const char* tag : kTags) {
        for (bool binary : {false, true}) {
            std::string name = std::string("acad_") + tag + "_dxf_native_records" + (binary ? "_bin" : "") + ".dxf";
            std::vector<uint8_t> in = facit_bytes(name);
            Drawing d = load(in, name);
            std::vector<const Object*> filters = objects_of(d, ObjectType::LayerFilter);
            assert_eq(1, static_cast<int>(filters.size()), name + ": a layer filter");
            assert_eq(2, static_cast<int>(filters[0]->as_layer_filter().layers.size()));
            assert_eq(std::string("HDXF_FILTER_A"), filters[0]->as_layer_filter().layers[0]);
            assert_eq(std::string("HDXF_FILTER_B"), filters[0]->as_layer_filter().layers[1]);
            assert_eq(1, static_cast<int>(objects_of(d, ObjectType::SpatialFilter).size()), "its INSERT's clip is in the same dictionary");
            assert_eq(1, static_cast<int>(objects_of(d, ObjectType::LayerIndex).size()), "INDEXCTL 3: the index");
            assert_eq(4, static_cast<int>(objects_of(d, ObjectType::IdBuffer).size()), "the user's IDBUFFER and one per layer");
            // The user's IDBUFFER (a list of two entities, owned by a dictionary) and an index's (owned by 0)
            int owned_by_dictionary = 0;
            for (const Object* o : objects_of(d, ObjectType::IdBuffer)) {
                if (o->owner != "0") {
                    ++owned_by_dictionary;
                    assert_eq(2, static_cast<int>(o->as_id_buffer().object_handles.size()), "two members");
                }
            }
            assert_eq(1, owned_by_dictionary, "one IDBUFFER has an owner dictionary");
            std::vector<uint8_t> out = save(d, binary ? DxfFormat::Binary : DxfFormat::Ascii, name);
            assert_true(out == in, name + ": written unchanged");
        }
    }
}

TEST(order_index_reads_acadsharps_sample_with_the_old_clip_and_draw_order) {
    std::filesystem::path path = hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / "acadsharp" / "sample_AC1015_ascii.dxf";
    std::vector<uint8_t> in = hdxf_test::read_file_bytes(path);
    if (in.empty()) hdxf_test::skip("missing acadsharp/sample_AC1015_ascii.dxf");
    Drawing d = load(in, "sample_AC1015");
    const Object* table = d.find_object("355");
    assert_true(table && table->type == ObjectType::SortentsTable, "SORTENTSTABLE 355");
    assert_eq(8, static_cast<int>(table->as_sortents_table().entries.size()), "eight pairs");
    assert_eq(std::string("353"), table->as_sortents_table().entries[0].entity_handle);
    assert_eq(std::string("35A"), table->as_sortents_table().entries[0].sort_key, "the sort handle is no pointer: 35A need not be an entity");
    const Object* filter = d.find_object("D32");
    assert_true(filter && filter->type == ObjectType::SpatialFilter, "SPATIAL_FILTER D32");
    assert_eq(2, static_cast<int>(filter->as_spatial_filter().boundary.size()));
    // The same file again, as the old reader read it: written unchanged
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "sample_AC1015");
    assert_true(out == in, "written unchanged");
}

TEST(order_index_autocads_header_extents_ignore_a_clip) {
    // AutoCAD's own extents of a drawing with a clipped INSERT (INSERT at x 100, clip x 105 … 150, geometry to x 160):
    // $EXTMAX x is 160, so the clip changes no extents and none are made of the SPATIAL_FILTER (B107)
    std::vector<uint8_t> in = facit_bytes("acad_2018_dxf_native_records.dxf");
    Drawing d = load(in, "native_records");
    const hdxf::HeaderVar* max = d.find_header("$EXTMAX");
    assert_true(max && !max->values.empty(), "$EXTMAX");
    assert_eq(160.0, std::atof(max->values[0].second.c_str()), "the unclipped INSERT's extent (clip to 150)");
    for (const hdxf::Entity& e : d.entities()) {
        if (e.type != hdxf::EntityType::Insert) continue;
        Point3 lo, hi;
        assert_true(d.entity_extents(e, lo, hi), "the INSERT has extents");
        assert_eq(160.0, hi.x, "HDxf's extent of the clipped INSERT is the unclipped one, as AutoCAD's");
    }
}

// ---------------------------------------------------------------------------
// Changes: written as the record with only the change, the others as they were (B12)
// ---------------------------------------------------------------------------

namespace {

struct Scenario {
    const char* name;
    void (*change)(Drawing&);
    void (*verify)(const Drawing& changed, const Drawing& before);
    std::vector<ObjectType> touches;        // the types of the records the change may write anew
};

const Object* model_table(const Drawing& d) { return find_by_type_and_index(d, ObjectType::SortentsTable, 0); }

std::set<std::string> handles_of_types(const Drawing& d, std::initializer_list<ObjectType> types) {
    std::set<std::string> out;
    for (const Object& o : d.objects()) {
        for (ObjectType t : types) if (o.type == t) out.insert(o.handle);
    }
    return out;
}

void run_scenario(const std::vector<uint8_t>& in, const Scenario& s, const std::string& file) {
    DxfFormat format = format_of(in);
    Drawing before = load(in, file);
    Drawing d = load(in, file);
    s.change(d);
    std::vector<uint8_t> out = save(d, format, file + ": " + s.name);
    assert_true(out != in, file + ": " + s.name + ": the change is written");
    Drawing after = load(out, file + ": " + s.name + " read back");
    s.verify(after, before);
    assert_true(std::none_of(after.warnings().begin(), after.warnings().end(),
                             [](const std::string& w) { return w.find("kept as it was read") != std::string::npos; }),
                file + ": " + s.name + ": every record of the family was read");
    // What was not changed is the record as it was
    std::set<std::string> except;
    for (const Object& o : before.objects()) {
        for (ObjectType t : s.touches) if (o.type == t) except.insert(o.handle);
    }
    for (const Object& o : before.objects()) {
        if (except.count(o.handle)) continue;
        const Object* now = after.find_object(o.handle);
        assert_true(now != nullptr, file + ": " + s.name + ": " + o.handle + " is gone");
        assert_true(same_groups(o, *now), file + ": " + s.name + ": " + o.handle + " changed (" + describe(*now) + ")");
    }
    // The records the change may write anew: those it did not touch are the same as before
    // (a second write of what was read is again the same bytes: B12 holds for the changed drawing too)
    std::vector<uint8_t> again = save(after, format, file + ": " + s.name + " again");
    assert_true(again == out, file + ": " + s.name + ": the changed file written again unchanged is the same bytes");
}

} // namespace

TEST(order_index_a_changed_value_is_written_in_its_place_and_the_other_records_stay) {
    const std::vector<Scenario> scenarios = {
        {"a scalar and a point of the clip",
         [](Drawing& d) {
             auto& f = objects_mut(d, ObjectType::SpatialFilter)[0]->as_spatial_filter();
             f.enabled = false;
             f.boundary[0].x += 1.5;
             f.origin = {1.0, 2.0, 3.0};
         },
         [](const Drawing& c, const Drawing& b) {
             const auto& f = objects_of(c, ObjectType::SpatialFilter)[0]->as_spatial_filter();
             const auto& g = objects_of(b, ObjectType::SpatialFilter)[0]->as_spatial_filter();
             assert_false(f.enabled, "71");
             assert_eq(g.boundary[0].x + 1.5, f.boundary[0].x, "10");
             assert_eq(g.boundary[0].y, f.boundary[0].y, "20 stays");
             assert_eq(2.0, f.origin.y, "11, 21, 31");
             assert_eq(g.inverse_insert_transform[3], f.inverse_insert_transform[3], "the matrices stay");
             assert_eq(2, static_cast<int>(f.boundary.size()), "70 stays");
         },
         {ObjectType::SpatialFilter}},
        {"clip planes made and taken away",
         [](Drawing& d) {
             auto filters = objects_mut(d, ObjectType::SpatialFilter);
             filters[0]->as_spatial_filter().front_distance = 7.5;          // the optional groups 72 (1) and 40
             filters[0]->as_spatial_filter().back_distance = -2.25;         // 73 (1) and 41
             filters[1]->as_spatial_filter().front_distance.reset();        // 72 (0), no 40
             filters[1]->as_spatial_filter().back_distance.reset();
         },
         [](const Drawing& c, const Drawing& b) {
             auto filters = objects_of(c, ObjectType::SpatialFilter);
             auto was = objects_of(b, ObjectType::SpatialFilter);
             assert_true(filters[0]->as_spatial_filter().front_distance && *filters[0]->as_spatial_filter().front_distance == 7.5, "front made");
             assert_true(filters[0]->as_spatial_filter().back_distance && *filters[0]->as_spatial_filter().back_distance == -2.25, "back made");
             assert_false(filters[1]->as_spatial_filter().front_distance.has_value(), "front taken away");
             assert_false(filters[1]->as_spatial_filter().back_distance.has_value(), "back taken away");
             // The matrices are still the twenty-four numbers they were: the 40 of the plane is not one of them
             assert_true(filters[0]->as_spatial_filter().clip_transform == was[0]->as_spatial_filter().clip_transform, "matrix 2");
             assert_true(filters[0]->as_spatial_filter().inverse_insert_transform == was[0]->as_spatial_filter().inverse_insert_transform, "matrix 1");
             assert_true(filters[1]->as_spatial_filter().clip_transform == was[1]->as_spatial_filter().clip_transform, "matrix 2 of the polygon");
             assert_true(filters[1]->as_spatial_filter().inverse_insert_transform == was[1]->as_spatial_filter().inverse_insert_transform, "matrix 1 of the polygon");
         },
         {ObjectType::SpatialFilter}},
        {"a longer boundary (the count 70 follows)",
         [](Drawing& d) {
             auto& f = objects_mut(d, ObjectType::SpatialFilter)[1]->as_spatial_filter();
             f.boundary.push_back({170.0, 120.0});
             f.boundary.erase(f.boundary.begin());
         },
         [](const Drawing& c, const Drawing& b) {
             const auto& f = objects_of(c, ObjectType::SpatialFilter)[1]->as_spatial_filter();
             const auto& g = objects_of(b, ObjectType::SpatialFilter)[1]->as_spatial_filter();
             assert_eq(static_cast<int>(g.boundary.size()), static_cast<int>(f.boundary.size()), "one out, one in");
             assert_eq(170.0, f.boundary.back().x);
             assert_eq(g.boundary[1].x, f.boundary[0].x);
             assert_true(f.front_distance && f.back_distance, "the planes stay");
         },
         {ObjectType::SpatialFilter}},
        {"a layer renamed, the timestamp moved and a layer added in the index",
         [](Drawing& d) {
             auto& index = objects_mut(d, ObjectType::LayerIndex)[0]->as_layer_index();
             index.entries[1].layer_name = "HDXF_A_RENAMED";
             index.timestamp += 1.5;
             index.entries.push_back({"HDXF_D", index.entries[0].idbuffer_handle, 0});
         },
         [](const Drawing& c, const Drawing& b) {
             const auto& index = objects_of(c, ObjectType::LayerIndex)[0]->as_layer_index();
             const auto& was = objects_of(b, ObjectType::LayerIndex)[0]->as_layer_index();
             assert_eq(static_cast<int>(was.entries.size()) + 1, static_cast<int>(index.entries.size()));
             assert_eq(std::string("HDXF_A_RENAMED"), index.entries[1].layer_name);
             assert_eq(was.entries[1].idbuffer_handle, index.entries[1].idbuffer_handle, "its IDBUFFER stays");
             assert_eq(was.entries[1].count, index.entries[1].count, "and its count");
             assert_eq(was.timestamp + 1.5, index.timestamp, "40");
             assert_eq(std::string("HDXF_D"), index.entries.back().layer_name);
         },
         {ObjectType::LayerIndex}},
        {"members of an IDBUFFER: one added, one taken, the index's number follows",
         [](Drawing& d) {
             auto buffers = objects_mut(d, ObjectType::IdBuffer);
             // the IDBUFFER of HDXF_A: three INSERTs; the first is taken, a line of our own is added
             const auto& index = objects_mut(d, ObjectType::LayerIndex)[0]->as_layer_index();
             Object* buffer = d.find_object(index.entries[1].idbuffer_handle);
             assert_true(buffer != nullptr, "HDXF_A's IDBUFFER");
             buffer->as_id_buffer().object_handles.erase(buffer->as_id_buffer().object_handles.begin());
             // an entity of the drawing that is not in it yet
             for (const hdxf::Entity& e : d.entities()) {
                 if (e.type == hdxf::EntityType::Circle) {
                     buffer->as_id_buffer().object_handles.push_back(e.props.handle);
                     buffer->as_id_buffer().object_handles.push_back(e.props.handle);
                     break;
                 }
             }
         },
         [](const Drawing& c, const Drawing& b) {
             const auto& index = objects_of(c, ObjectType::LayerIndex)[0]->as_layer_index();
             const auto& was = objects_of(b, ObjectType::LayerIndex)[0]->as_layer_index();
             const Object* buffer = c.find_object(index.entries[1].idbuffer_handle);
             assert_eq(4, static_cast<int>(buffer->as_id_buffer().object_handles.size()), "3 - 1 + 2");
             assert_eq(was.entries[1].count - 1 + 2, index.entries[1].count, "the number the index writes follows the members");
             assert_eq(was.entries[2].count, index.entries[2].count, "the other layers stay");
         },
         {ObjectType::IdBuffer, ObjectType::LayerIndex}},
        {"draw order: keys exchanged, a pair added and a pair taken",
         [](Drawing& d) {
             auto& table = objects_mut(d, ObjectType::SortentsTable)[0]->as_sortents_table();
             std::swap(table.entries[0].sort_key, table.entries[1].sort_key);
             table.entries.erase(table.entries.begin() + 2);
             table.entries.push_back({table.entries[0].entity_handle, "FFFF"});
         },
         [](const Drawing& c, const Drawing& b) {
             const auto& table = model_table(c)->as_sortents_table();
             const auto& was = model_table(b)->as_sortents_table();
             assert_eq(static_cast<int>(was.entries.size()), static_cast<int>(table.entries.size()), "-1 +1");
             assert_eq(was.entries[1].sort_key, table.entries[0].sort_key, "keys exchanged");
             assert_eq(was.entries[0].sort_key, table.entries[1].sort_key);
             assert_eq(was.entries[3].entity_handle, table.entries[2].entity_handle, "the third pair is gone");
             assert_eq(std::string("FFFF"), table.entries.back().sort_key);
             assert_eq(was.block_record_handle, table.block_record_handle);
             assert_eq(static_cast<int>(objects_of(b, ObjectType::SortentsTable)[1]->as_sortents_table().entries.size()),
                       static_cast<int>(objects_of(c, ObjectType::SortentsTable)[1]->as_sortents_table().entries.size()), "Layout1's stays");
         },
         {ObjectType::SortentsTable}},
        {"the spatial index: the timestamp and the tree",
         [](Drawing& d) {
             auto& si = objects_mut(d, ObjectType::SpatialIndex)[0]->as_spatial_index();
             si.timestamp += 2.0;
             si.tree->data.push_back(0x5A);          // the length (the second 90) follows
             si.tree->values[3] = -1.0;
         },
         [](const Drawing& c, const Drawing& b) {
             const auto& si = objects_of(c, ObjectType::SpatialIndex)[0]->as_spatial_index();
             const auto& was = objects_of(b, ObjectType::SpatialIndex)[0]->as_spatial_index();
             assert_eq(was.timestamp + 2.0, si.timestamp);
             assert_eq(static_cast<int>(was.tree->data.size()) + 1, static_cast<int>(si.tree->data.size()), "90 follows the bytes");
             assert_eq(0x5A, static_cast<int>(si.tree->data.back()));
             assert_eq(-1.0, si.tree->values[3]);
             assert_eq(was.tree->values[2], si.tree->values[2]);
         },
         {ObjectType::SpatialIndex}},
        {"the spatial index written empty (the reference's form)",
         [](Drawing& d) { objects_mut(d, ObjectType::SpatialIndex)[0]->as_spatial_index().tree.reset(); },
         [](const Drawing& c, const Drawing&) {
             assert_false(objects_of(c, ObjectType::SpatialIndex)[0]->as_spatial_index().tree.has_value(), "no tree");
         },
         {ObjectType::SpatialIndex}},
    };
    for (const auto& file : {order_index_file("2018", false), order_index_file("2010", true), order_index_file("2007", false),
                             order_index_file("2013", true)}) {
        std::vector<uint8_t> in = facit_bytes(file);
        for (const Scenario& s : scenarios) run_scenario(in, s, file);
    }
}

TEST(order_index_layer_filter_changes_follow_the_list) {
    for (bool binary : {false, true}) {
        std::string name = std::string("acad_2018_dxf_native_records") + (binary ? "_bin" : "") + ".dxf";
        std::vector<uint8_t> in = facit_bytes(name);
        Drawing d = load(in, name);
        auto& layers = objects_mut(d, ObjectType::LayerFilter)[0]->as_layer_filter().layers;
        layers.erase(layers.begin());
        layers.push_back("HDXF_FILTER_C");
        layers.push_back("Lager å ∅");
        std::vector<uint8_t> out = save(d, format_of(in), name);
        Drawing r = load(out, "changed");
        const auto& now = objects_of(r, ObjectType::LayerFilter)[0]->as_layer_filter().layers;
        assert_eq(3, static_cast<int>(now.size()));
        assert_eq(std::string("HDXF_FILTER_B"), now[0]);
        assert_eq(std::string("HDXF_FILTER_C"), now[1]);
        assert_eq(std::string("Lager å ∅"), now[2], "UTF-8 in 2018");
    }
}

TEST(order_index_typed_values_written_without_the_preserved_record_read_back_the_same) {
    // The preserved record emptied: the record is written from the typed values alone, and reads back as the same model
    for (const std::string& file : {order_index_file("2018", false), order_index_file("2018", true), order_index_file("2010", false),
                                    order_index_file("2000", true), std::string("acad_2013_dxf_native_records.dxf")}) {
        std::vector<uint8_t> in = facit_bytes(file);
        Drawing d = load(in, file);
        std::vector<std::string> before = describe_all(d);
        assert_true(before.size() >= 3, "the family's records");
        for (Object& o : d.objects()) if (is_order_index_type(o.type)) o.raw = hdxf::RawRecord();
        std::vector<uint8_t> out = save(d, format_of(in), file);
        Drawing r = load(out, file + " rewritten");
        assert_true(std::none_of(r.warnings().begin(), r.warnings().end(),
                                 [](const std::string& w) { return w.find("kept as it was read") != std::string::npos; }), "typed again");
        std::vector<std::string> after = describe_all(r);
        assert_eq(static_cast<int>(before.size()), static_cast<int>(after.size()), file + ": the same records");
        for (size_t i = 0; i < before.size(); ++i) assert_eq(before[i], after[i], file + ": record " + std::to_string(i));
        // and written again, the same bytes (the rewritten records are now the preserved ones)
        std::vector<uint8_t> again = save(r, format_of(in), file);
        assert_true(again == out, file + ": written again unchanged");
    }
}

// ---------------------------------------------------------------------------
// B19: removals follow
// ---------------------------------------------------------------------------

namespace {

bool has_warning_text(const Drawing& d, const std::string& part) {
    return std::any_of(d.warnings().begin(), d.warnings().end(), [&](const std::string& w) { return w.find(part) != std::string::npos; });
}

int class_instances(const std::vector<uint8_t>& ascii, const std::string& name) {
    for (const Rec& r : records_named(ascii_records(ascii), "CLASS")) {
        if (r.size() > 1 && trim(r[1].second) == name) {
            for (const Group& g : r) if (g.first == 91) return std::atoi(g.second.c_str());
        }
    }
    return -1;
}

void remove_entity(Drawing& d, const std::string& handle) {
    auto& list = d.entities();
    auto it = std::find_if(list.begin(), list.end(), [&](const hdxf::Entity& e) { return e.props.handle == handle; });
    assert_true(it != list.end(), "the entity " + handle);
    list.erase(it);
}

} // namespace

TEST(order_index_removing_an_entity_takes_its_pair_member_clip_and_count) {
    for (const std::string& file : {order_index_file("2018", false), order_index_file("2013", true)}) {
        std::vector<uint8_t> in = facit_bytes(file);
        Drawing before = load(in, file);
        Drawing d = load(in, file);

        // The first INSERT: it has a draw order pair, is a member of HDXF_A's IDBUFFER and has a clip (its extension
        // dictionary with ACAD_FILTER with SPATIAL with the SPATIAL_FILTER). Its handle is also a sort key (5) of another pair.
        std::string insert;
        for (const hdxf::Entity& e : d.entities()) {
            if (e.type == hdxf::EntityType::Insert) { insert = e.props.handle; break; }
        }
        assert_false(insert.empty(), "an INSERT");
        const Object* table = model_table(before);
        int pairs = static_cast<int>(table->as_sortents_table().entries.size());
        int keyed_by_it = 0, pointing_at_it = 0;
        for (const auto& e : table->as_sortents_table().entries) {
            if (e.sort_key == insert) ++keyed_by_it;
            if (e.entity_handle == insert) ++pointing_at_it;
        }
        assert_eq(1, pointing_at_it, "the INSERT has a pair");
        assert_eq(1, keyed_by_it, "and its handle is the key of another pair");
        size_t objects_before = before.objects().size();

        remove_entity(d, insert);
        std::vector<uint8_t> out = save(d, format_of(in), file);
        Drawing r = load(out, file + " changed");

        const Object* now = model_table(r);
        assert_eq(pairs - 1, static_cast<int>(now->as_sortents_table().entries.size()), file + ": the pair of the removed entity is gone");
        keyed_by_it = pointing_at_it = 0;
        for (const auto& e : now->as_sortents_table().entries) {
            if (e.sort_key == insert) ++keyed_by_it;
            if (e.entity_handle == insert) ++pointing_at_it;
        }
        assert_eq(0, pointing_at_it, "no pair points at it");
        assert_eq(1, keyed_by_it, "a sort key that is no pointer stays (B19, B104)");
        // Layout1's draw order is untouched
        assert_eq(4, static_cast<int>(objects_of(r, ObjectType::SortentsTable)[1]->as_sortents_table().entries.size()));

        // The index: HDXF_A's IDBUFFER has two members and the index says two
        const auto& index = objects_of(r, ObjectType::LayerIndex)[0]->as_layer_index();
        assert_eq(std::string("HDXF_A"), index.entries[1].layer_name);
        assert_eq(2, index.entries[1].count, file + ": the count follows the member that went");
        const Object* buffer = r.find_object(index.entries[1].idbuffer_handle);
        assert_eq(2, static_cast<int>(buffer->as_id_buffer().object_handles.size()));
        for (const std::string& h : buffer->as_id_buffer().object_handles) assert_true(h != insert, "the member is gone");
        assert_eq(4, index.entries[2].count, "HDXF_B is as it was");

        // The clip: the INSERT's extension dictionary and everything under it go (three records)
        assert_eq(2, static_cast<int>(objects_of(r, ObjectType::SpatialFilter).size()), "its SPATIAL_FILTER is gone");
        assert_eq(static_cast<int>(objects_before) - 3, static_cast<int>(r.objects().size()), "the extension dictionary, ACAD_FILTER and the filter");

        // CLASSES of a file read are the file's own (every type): the class stays, with the count the file had
        if (format_of(in) == DxfFormat::Ascii) {
            assert_eq(class_instances(in, "SPATIAL_FILTER"), class_instances(out, "SPATIAL_FILTER"), "SPATIAL_FILTER in CLASSES as read");
        }

        // The records the removal did not reach are as they were
        expect_other_records_as_they_were(before, r,
            [&] {
                std::set<std::string> touched = handles_of_types(before, {ObjectType::SortentsTable, ObjectType::LayerIndex, ObjectType::IdBuffer});
                for (const Object& o : before.objects()) {
                    // the INSERT's chain and its dictionary entries
                    if (r.find_object(o.handle) == nullptr) touched.insert(o.handle);
                }
                // the dictionaries that listed what was removed
                for (const Object& o : before.objects()) {
                    if (o.type == ObjectType::Dictionary) touched.insert(o.handle);
                }
                return touched;
            }(),
            file);
    }
}

TEST(order_index_an_idbuffer_the_layer_index_lets_go_of_goes_with_it) {
    // An IDBUFFER has the owner 0 in AutoCAD's files: the layer index owns it through the 360 of its entry (hard owner)
    for (const std::string& file : {order_index_file("2018", false), order_index_file("2010", true)}) {
        std::vector<uint8_t> in = facit_bytes(file);
        Drawing before = load(in, file);
        Drawing d = load(in, file);
        assert_eq(4, static_cast<int>(objects_of(d, ObjectType::IdBuffer).size()));
        for (const Object* o : objects_of(d, ObjectType::IdBuffer)) assert_eq(std::string("0"), o->owner);

        // HDXF_B's entry taken out of the index: its IDBUFFER goes (nothing owns it any more), the others stay
        std::string buffer_handle = objects_of(d, ObjectType::LayerIndex)[0]->as_layer_index().entries[2].idbuffer_handle;
        auto& entries = objects_mut(d, ObjectType::LayerIndex)[0]->as_layer_index().entries;
        assert_eq(std::string("HDXF_B"), entries[2].layer_name);
        entries.erase(entries.begin() + 2);
        std::vector<uint8_t> out = save(d, format_of(in), file);
        Drawing r = load(out, "entry removed");
        assert_eq(3, static_cast<int>(objects_of(r, ObjectType::LayerIndex)[0]->as_layer_index().entries.size()), "three entries");
        assert_eq(3, static_cast<int>(objects_of(r, ObjectType::IdBuffer).size()), file + ": HDXF_B's IDBUFFER is gone");
        assert_true(r.find_object(buffer_handle) == nullptr, "that one");
        for (const Object* o : objects_of(r, ObjectType::IdBuffer)) assert_true(o->handle != buffer_handle, "no other");
        assert_eq(1, static_cast<int>(objects_of(r, ObjectType::SpatialIndex).size()), "the spatial index is another record");
        // the others are as the file had them
        std::set<std::string> except = {objects_of(before, ObjectType::LayerIndex)[0]->handle, buffer_handle};
        for (const Object& o : before.objects()) {
            if (o.type == ObjectType::Dictionary || except.count(o.handle)) continue;     // (a dictionary may list the removed)
            const Object* now = r.find_object(o.handle);
            assert_true(now != nullptr, o.handle + " is there");
            assert_true(same_groups(o, *now), o.handle + " is as it was");
        }

        // The entry whose IDBUFFER is removed from the objects: the entry goes whole (name, owner and count)
        Drawing e = load(in, file);
        std::string second = objects_of(e, ObjectType::LayerIndex)[0]->as_layer_index().entries[1].idbuffer_handle;
        auto& objs = e.objects();
        objs.erase(std::find_if(objs.begin(), objs.end(), [&](const Object& o) { return o.handle == second; }));
        std::vector<uint8_t> out2 = save(e, format_of(in), file);
        Drawing r2 = load(out2, "one IDBUFFER removed");
        const auto& left = objects_of(r2, ObjectType::LayerIndex)[0]->as_layer_index().entries;
        assert_eq(3, static_cast<int>(left.size()), "the entry whose IDBUFFER is gone is gone");
        for (const auto& en : left) assert_true(en.layer_name != "HDXF_A", "HDXF_A's entry");
        assert_eq(std::string("0"), left[0].layer_name);
        assert_eq(std::string("HDXF_B"), left[1].layer_name);
        assert_eq(4, left[1].count);

        // The layer index removed from the objects altogether: every IDBUFFER it owned goes with it (B19), the
        // user's own IDBUFFERs (none in this file) would stay
        Drawing x = load(in, file);
        std::string index_handle = objects_of(x, ObjectType::LayerIndex)[0]->handle;
        auto& xobjs = x.objects();
        xobjs.erase(std::find_if(xobjs.begin(), xobjs.end(), [&](const Object& o) { return o.handle == index_handle; }));
        std::vector<uint8_t> out3 = save(x, format_of(in), file);
        Drawing r3 = load(out3, "layer index removed");
        assert_eq(0, static_cast<int>(objects_of(r3, ObjectType::LayerIndex).size()), "the index is gone");
        assert_eq(0, static_cast<int>(objects_of(r3, ObjectType::IdBuffer).size()), file + ": its IDBUFFERs went with it");
        assert_eq(1, static_cast<int>(objects_of(r3, ObjectType::SpatialIndex).size()), "the spatial index stays");

        // Nothing removed: nothing dropped, and the file is the file
        Drawing same = load(in, file);
        assert_true(save(same, format_of(in), file) == in, "nothing changed");
    }
}

TEST(order_index_the_inverted_clip_has_its_round_trip_xrecord_under_the_filter) {
    // AutoCAD's inverted clip (XCLIP, Invert clip): the SPATIAL_FILTER holds the inverted polygon, and its extension
    // dictionary ACAD_XREC_ROUNDTRIP the XRECORD with the boundary the user drew and the polygon (the same points)
    for (const std::string& file : {order_index_file("2018", false), order_index_file("2010", true)}) {
        std::vector<uint8_t> in = facit_bytes(file);
        Drawing d = load(in, file);
        const Object* filter = objects_of(d, ObjectType::SpatialFilter)[2];
        const Object* dict = d.find_object(filter->xdictionary);
        assert_true(dict && dict->type == ObjectType::Dictionary, "the extension dictionary");
        assert_eq(1, static_cast<int>(dict->as_dictionary().entries.size()));
        assert_eq(std::string("ACAD_XREC_ROUNDTRIP"), dict->as_dictionary().entries[0].name);
        const Object* xrecord = d.find_object(dict->as_dictionary().entries[0].handle);
        assert_true(xrecord && xrecord->type == ObjectType::XRecord, "the XRECORD");
        const auto& data = xrecord->as_xrecord().data;
        // 102 ACAD_INVERTEDCLIP_ROUNDTRIP, the rectangle's two corners (10 20 30), 102 …_COMPARE, the polygon's points
        assert_true(!data.empty() && data[0].code == 102 && data[0].value == "ACAD_INVERTEDCLIP_ROUNDTRIP", "the user's boundary");
        size_t compare = 0;
        for (size_t i = 0; i < data.size(); ++i) if (data[i].code == 102 && data[i].value == "ACAD_INVERTEDCLIP_ROUNDTRIP_COMPARE") compare = i;
        assert_true(compare > 0, "the polygon marker");
        assert_eq(1 + 6, static_cast<int>(compare), "two corners of three coordinates");
        const auto& polygon = filter->as_spatial_filter().boundary;
        assert_eq(static_cast<int>(polygon.size()) * 3, static_cast<int>(data.size() - compare - 1), "the polygon's points");
        for (size_t k = 0; k < polygon.size(); ++k) {
            assert_eq(polygon[k].x, std::atof(data[compare + 1 + 3 * k].value.c_str()), "10 of the polygon = the filter's 10");
            assert_eq(polygon[k].y, std::atof(data[compare + 2 + 3 * k].value.c_str()), "20");
        }

        // The INSERT removed: the filter, its extension dictionary and the XRECORD go with it
        std::string insert;
        for (const hdxf::Entity& e : d.entities()) {
            if (e.type == hdxf::EntityType::Insert && e.props.handle == [&] {
                    // the INSERT whose ACAD_FILTER dictionary reaches the inverted filter
                    for (const Object& o : d.objects()) if (o.type == ObjectType::Dictionary && o.handle == filter->owner) return o.owner;
                    return std::string();
                }()) {
                insert = e.props.handle;
            }
        }
        // (the filter's owner is the ACAD_FILTER dictionary of the SPATIAL entry, and its owner the extension dictionary)
        std::string spatial = filter->owner;                             // the SPATIAL dictionary's owner chain
        const Object* spatial_dict = d.find_object(spatial);
        const Object* filter_dict = spatial_dict ? d.find_object(spatial_dict->owner) : nullptr;
        const Object* ext_dict = filter_dict ? d.find_object(filter_dict->owner) : nullptr;
        (void)ext_dict;
        // The INSERT is the third one (y 200)
        std::vector<std::string> inserts;
        for (const hdxf::Entity& e : d.entities()) if (e.type == hdxf::EntityType::Insert) inserts.push_back(e.props.handle);
        assert_eq(3, static_cast<int>(inserts.size()), "three INSERTs");
        remove_entity(d, inserts[2]);
        std::vector<uint8_t> out = save(d, format_of(in), file);
        Drawing r = load(out, "the inverted clip's INSERT removed");
        assert_eq(2, static_cast<int>(objects_of(r, ObjectType::SpatialFilter).size()), "the inverted filter is gone");
        for (const Object* o : objects_of(r, ObjectType::XRecord)) {
            for (const auto& g : o->as_xrecord().data) {
                assert_false(g.code == 102 && g.value.rfind("ACAD_INVERTEDCLIP", 0) == 0, "its round trip XRECORD is gone");
            }
        }
        assert_true(r.find_object(xrecord->handle) == nullptr && r.find_object(dict->handle) == nullptr, "the XRECORD and its dictionary");
    }
}

// ---------------------------------------------------------------------------
// New records: all versions and both formats (B101, B114)
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kLineA = "7A10";
constexpr const char* kLineB = "7A11";

hdxf::SpatialFilterData a_filter() {
    hdxf::SpatialFilterData f;
    f.boundary = {{1.5, 2.5}, {11.5, 12.5}, {20.25, 3.75}};
    f.normal = {0.0, 0.0, -1.0};
    f.origin = {1.0, 2.0, 3.0};
    f.enabled = true;
    f.front_distance = 10.5;
    f.back_distance = -4.0;
    f.inverse_insert_transform = {1, 0, 0, -5, 0, 1, 0, -6, 0, 0, 1, -7};
    f.clip_transform = {0.5, 0, 0, 1, 0, 0.5, 0, 2, 0, 0, 0.5, 3};
    return f;
}

void report_matrix(const std::string& name, const hdxf_test::MatrixReport& report) {
    std::cout << "    [order_index] " << name << ": " << report.generated << " cells generated, " << report.named_errors
              << " named errors, " << report.not_established << " not established; AutoCAD " << report.autocad_verified
              << " verified, " << report.autocad_not_tested << " ej testat, " << report.autocad_not_savable << " not savable\n";
}

} // namespace

TEST(order_index_every_type_is_made_without_a_preserved_record_in_every_version_and_format) {
    const TypedRegistry& registry = TypedRegistry::instance();

    // SORTENTSTABLE
    {
        const ObjectCodec* codec = registry.find_object("SORTENTSTABLE");
        assert_true(codec && codec->read && codec->write && codec->step == 4, "a codec");
        auto add = [](Drawing& d) {
            line_with_handle(d, kLineA, 0);
            line_with_handle(d, kLineB, 5);
            hdxf::SortentsTableData t;
            t.entries = {{kLineB, "2A"}, {kLineA, "2B"}, {kLineB, "FFFF"}};     // a key that is no handle of any record
            d.objects().push_back(make_object(ObjectType::SortentsTable, "7B01", t));
        };
        auto verify = [](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            const Object* o = find_by_type_and_index(r, ObjectType::SortentsTable, 0);
            if (!o) return "no SORTENTSTABLE";
            const auto& e = o->as_sortents_table().entries;
            if (e.size() != 3) return "pairs: " + std::to_string(e.size());
            if (e[0].entity_handle != kLineB || e[0].sort_key != "2A" || e[1].sort_key != "2B" || e[2].sort_key != "FFFF") return "pairs differ";
            return {};
        };
        report_matrix("SORTENTSTABLE", hdxf_test::run_version_matrix("SORTENTSTABLE", codec->versions, add, verify));
    }
    // SPATIAL_FILTER
    {
        const ObjectCodec* codec = registry.find_object("SPATIAL_FILTER");
        assert_true(codec != nullptr, "a codec");
        auto add = [](Drawing& d) { d.objects().push_back(make_object(ObjectType::SpatialFilter, "7B02", a_filter())); };
        auto verify = [](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            const Object* o = find_by_type_and_index(r, ObjectType::SpatialFilter, 0);
            if (!o) return "no SPATIAL_FILTER";
            const hdxf::SpatialFilterData want = a_filter();
            const auto& f = o->as_spatial_filter();
            if (f.boundary.size() != 3 || f.boundary[2].x != 20.25 || f.boundary[0].y != 2.5) return "boundary";
            if (f.normal.z != -1.0 || f.origin.z != 3.0 || !f.enabled) return "normal, origin, enabled";
            if (!f.front_distance || *f.front_distance != 10.5 || !f.back_distance || *f.back_distance != -4.0) return "planes";
            if (f.inverse_insert_transform != want.inverse_insert_transform || f.clip_transform != want.clip_transform) return "matrices";
            return {};
        };
        report_matrix("SPATIAL_FILTER", hdxf_test::run_version_matrix("SPATIAL_FILTER", codec->versions, add, verify));
    }
    // IDBUFFER, with a LAYER_INDEX that owns it
    {
        const ObjectCodec* codec = registry.find_object("IDBUFFER");
        assert_true(codec != nullptr, "a codec");
        auto add = [](Drawing& d) {
            line_with_handle(d, kLineA, 0);
            line_with_handle(d, kLineB, 5);
            hdxf::IdBufferData b;
            b.object_handles = {kLineA, kLineB};
            d.objects().push_back(make_object(ObjectType::IdBuffer, "7B03", b));
            d.objects().push_back(make_object(ObjectType::IdBuffer, "7B04", hdxf::IdBufferData{}));        // an empty one
        };
        auto verify = [](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            auto buffers = objects_of(r, ObjectType::IdBuffer);
            if (buffers.size() != 2) return "buffers: " + std::to_string(buffers.size());
            if (buffers[0]->as_id_buffer().object_handles.size() != 2 || buffers[0]->as_id_buffer().object_handles[1] != kLineB) return "members";
            if (!buffers[1]->as_id_buffer().object_handles.empty()) return "an empty one";
            return {};
        };
        report_matrix("IDBUFFER", hdxf_test::run_version_matrix("IDBUFFER", codec->versions, add, verify));
    }
    // LAYER_INDEX (its IDBUFFERs: the count is derived)
    {
        const ObjectCodec* codec = registry.find_object("LAYER_INDEX");
        assert_true(codec != nullptr, "a codec");
        auto add = [](Drawing& d) {
            line_with_handle(d, kLineA, 0);
            line_with_handle(d, kLineB, 5);
            hdxf::LayerIndexData index;
            index.timestamp = 2461000.5;
            index.entries = {{"0", "7B03", 99}, {"Walls", "7B04", 5}};      // 99: the IDBUFFER has two; 5: it has none
            d.objects().push_back(make_object(ObjectType::LayerIndex, "7B05", index));
            hdxf::IdBufferData b;
            b.object_handles = {kLineA, kLineB};
            d.objects().push_back(make_object(ObjectType::IdBuffer, "7B03", b));
            d.objects().push_back(make_object(ObjectType::IdBuffer, "7B04", hdxf::IdBufferData{}));
        };
        auto verify = [](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            const Object* o = find_by_type_and_index(r, ObjectType::LayerIndex, 0);
            if (!o) return "no LAYER_INDEX";
            const auto& i = o->as_layer_index();
            if (i.timestamp != 2461000.5) return "timestamp";
            if (i.entries.size() != 2 || i.entries[0].layer_name != "0" || i.entries[1].layer_name != "Walls") return "entries";
            if (i.entries[0].count != 2) return "the count follows the IDBUFFER: " + std::to_string(i.entries[0].count);
            if (i.entries[1].count != 0) return "an empty IDBUFFER's count: " + std::to_string(i.entries[1].count);
            if (i.entries[0].idbuffer_handle != "7B03") return "the IDBUFFER";
            return {};
        };
        report_matrix("LAYER_INDEX", hdxf_test::run_version_matrix("LAYER_INDEX", codec->versions, add, verify));
    }
    // SPATIAL_INDEX
    {
        const ObjectCodec* codec = registry.find_object("SPATIAL_INDEX");
        assert_true(codec != nullptr, "a codec");
        auto add = [](Drawing& d) {
            hdxf::SpatialIndexData s;
            s.timestamp = 2461000.25;
            hdxf::SpatialIndexTree tree;
            tree.values = {1.5, -2.5, 3.0, 4.0, 1e10, -0.0000000116415251};
            for (int i = 0; i < 300; ++i) tree.data.push_back(static_cast<uint8_t>(i * 7));      // more than one 310 group
            s.tree = tree;
            d.objects().push_back(make_object(ObjectType::SpatialIndex, "7B06", s));
            d.objects().push_back(make_object(ObjectType::SpatialIndex, "7B07", hdxf::SpatialIndexData{}));      // the reference's empty one
        };
        auto verify = [](const Drawing& r, DxfVersion v, DxfFormat) -> std::string {
            auto list = objects_of(r, ObjectType::SpatialIndex);
            if (list.size() != 2) return "records: " + std::to_string(list.size());
            const auto& s = list[0]->as_spatial_index();
            if (s.timestamp != 2461000.25) return "timestamp";
            if (v >= DxfVersion::R2000) {
                if (!s.tree) return "the tree";
                if (s.tree->data.size() != 300 || s.tree->data[299] != static_cast<uint8_t>(299 * 7) || s.tree->values[5] != -0.0000000116415251) return "tree contents";
            } else if (s.tree) {
                return "R13 and R14 have the index empty";
            }
            if (list[1]->as_spatial_index().tree) return "an empty one has no tree";
            return {};
        };
        report_matrix("SPATIAL_INDEX", hdxf_test::run_version_matrix("SPATIAL_INDEX", codec->versions, add, verify));
    }
    // LAYER_FILTER
    {
        const ObjectCodec* codec = registry.find_object("LAYER_FILTER");
        assert_true(codec != nullptr, "a codec");
        auto add = [](Drawing& d) {
            hdxf::LayerFilterData f;
            f.layers = {"WALLS", "DOORS", "Annotation"};
            d.objects().push_back(make_object(ObjectType::LayerFilter, "7B08", f));
        };
        auto verify = [](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            const Object* o = find_by_type_and_index(r, ObjectType::LayerFilter, 0);
            if (!o) return "no LAYER_FILTER";
            const auto& l = o->as_layer_filter().layers;
            if (l.size() != 3 || l[0] != "WALLS" || l[2] != "Annotation") return "layers";
            return {};
        };
        report_matrix("LAYER_FILTER", hdxf_test::run_version_matrix("LAYER_FILTER", codec->versions, add, verify));
    }
}

TEST(order_index_the_version_matrix_says_what_autocad_has_and_has_not_shown) {
    const TypedRegistry& registry = TypedRegistry::instance();
    for (const char* name : {"SORTENTSTABLE", "SPATIAL_FILTER", "LAYER_INDEX", "SPATIAL_INDEX", "IDBUFFER", "LAYER_FILTER"}) {
        const ObjectCodec* codec = registry.find_object(name);
        assert_true(codec != nullptr, std::string(name) + ": a codec");
        assert_true(registry.implemented(name), "implemented");
        const VersionMatrix& m = codec->versions;
        // R12 has no OBJECTS: a named error, and AutoCAD's own R12 file shows neither the record nor the section
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            assert_true(m.at(DxfVersion::R12, f).generation == Generation::None && m.at(DxfVersion::R12, f).basis == ErrorBasis::Documented,
                        std::string(name) + ": R12 is a documented version error");
            for (DxfVersion v : kVersions) {
                assert_true(m.at(v, f).generation != Generation::None, std::string(name) + ": generated in 2000 and later");
                assert_true(m.at(v, f).autocad == AutoCadStatus::Verified, std::string(name) + ": AutoCAD 2025 saved it");
            }
            assert_true(m.at(DxfVersion::R13, f).autocad == AutoCadStatus::NotSavable, "R13 cannot be saved by AutoCAD 2025: ej testat");
            assert_true(m.at(DxfVersion::R14, f).autocad == AutoCadStatus::NotSavable, "R14 too");
        }
        // (the first version both formats can make it in: R13 for five, 2000 for LAYER_FILTER whose R13/R14 form is not established)
        const char* first = std::string(name) == "LAYER_FILTER" ? "2000" : "R13";
        assert_eq(std::string(name) + " requires DXF " + first + " or later", version_error_text(name, m, DxfVersion::R12));
    }
    const VersionMatrix& index = registry.find_object("SPATIAL_INDEX")->versions;
    assert_true(index.at(DxfVersion::R14, DxfFormat::Ascii).generation == Generation::OlderForm, "R13/R14: the empty form");
    const VersionMatrix& filter = registry.find_object("LAYER_FILTER")->versions;
    assert_true(filter.at(DxfVersion::R14, DxfFormat::Ascii).generation == Generation::None &&
                filter.at(DxfVersion::R14, DxfFormat::Ascii).basis == ErrorBasis::NotEstablished, "R13/R14: not established, no version error claimed");
}

TEST(order_index_a_layer_filter_is_not_made_in_r13_or_r14_and_nothing_is_written) {
    for (DxfVersion v : {DxfVersion::R13, DxfVersion::R14}) {
        Drawing d(v);
        hdxf::LayerFilterData f;
        f.layers = {"WALLS"};
        d.objects().push_back(make_object(ObjectType::LayerFilter, "7B08", f));
        size_t objects = d.objects().size();
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(out.empty(), "nothing is written");
        assert_true(d.last_error().find("LAYER_FILTER") != std::string::npos && d.last_error().find("not established") != std::string::npos,
                    d.last_error());
        assert_eq(static_cast<int>(objects), static_cast<int>(d.objects().size()), "the drawing is as it was");
        assert_eq(std::string("7B08"), d.objects().back().handle, "and the record has its handle");
    }
}

// ---------------------------------------------------------------------------
// Classes, names, text
// ---------------------------------------------------------------------------

TEST(order_index_new_drawings_get_the_classes_as_autocad_lists_them) {
    struct Want {
        const char* name;
        const char* cpp;
    };
    const Want wants[] = {{"LAYER_INDEX", "AcDbLayerIndex"}, {"SPATIAL_INDEX", "AcDbSpatialIndex"}, {"IDBUFFER", "AcDbIdBuffer"},
                          {"SPATIAL_FILTER", "AcDbSpatialFilter"}, {"LAYER_FILTER", "AcDbLayerFilter"},
                          {"SORTENTSTABLE", "AcDbSortentsTable"}};
    for (DxfVersion v : {DxfVersion::R13, DxfVersion::R14, DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2010, DxfVersion::R2018}) {
        Drawing d(v);
        line_with_handle(d, kLineA);
        hdxf::IdBufferData b;
        b.object_handles = {kLineA};
        d.objects().push_back(make_object(ObjectType::IdBuffer, "7B03", b));
        d.objects().push_back(make_object(ObjectType::IdBuffer, "7B04", hdxf::IdBufferData{}));
        hdxf::LayerIndexData index;
        index.entries = {{"0", "7B03", 1}};
        d.objects().push_back(make_object(ObjectType::LayerIndex, "7B05", index));
        d.objects().push_back(make_object(ObjectType::SpatialIndex, "7B06", hdxf::SpatialIndexData{}));
        d.objects().push_back(make_object(ObjectType::SpatialFilter, "7B02", a_filter()));
        hdxf::SortentsTableData t;
        t.entries = {{kLineA, "2A"}};
        d.objects().push_back(make_object(ObjectType::SortentsTable, "7B01", t));
        if (v >= DxfVersion::R2000) {
            hdxf::LayerFilterData f;
            f.layers = {"0"};
            d.objects().push_back(make_object(ObjectType::LayerFilter, "7B08", f));
        }
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, hdxf_test::version_label(v));
        std::vector<Rec> classes = records_named(ascii_records(out), "CLASS");
        std::vector<std::string> order;
        for (const Rec& r : classes) {
            std::string name = r.size() > 1 ? trim(r[1].second) : "";
            for (const Want& w : wants) {
                if (name != w.name) continue;
                order.push_back(name);
                assert_eq(std::string(w.cpp), trim(r[2].second), name + ": the C++ class");
                int flags = 0, instances = -1;
                for (const Group& g : r) {
                    if (g.first == 90) flags = std::atoi(g.second.c_str());
                    if (g.first == 91) instances = std::atoi(g.second.c_str());
                }
                if (v >= DxfVersion::R2000) {
                    assert_eq(std::string("ObjectDBX Classes"), trim(r[3].second), name + ": the application");
                    assert_eq(0, flags, "no proxy flags");
                    if (v >= DxfVersion::R2004) assert_eq(name == "IDBUFFER" ? 2 : 1, instances, name + ": instances");
                } else {
                    assert_eq(std::string(name == "SORTENTSTABLE" ? "\"AutoCAD\"" : "\"Acad\""), trim(r[3].second), name + ": R14's application, with its quotes");
                    assert_eq(v == DxfVersion::R13 ? 32768 : 0, flags, name + ": R13's proxy bit");
                }
            }
        }
        size_t expected = v >= DxfVersion::R2000 ? 6 : 5;
        assert_eq(static_cast<int>(expected), static_cast<int>(order.size()), std::string(hdxf_test::version_label(v)) + ": one class each, no more");
        if (v >= DxfVersion::R2000) {
            // The order of AutoCAD's own files
            std::vector<std::string> want_order = {"LAYER_INDEX", "SPATIAL_INDEX", "IDBUFFER", "SPATIAL_FILTER", "LAYER_FILTER", "SORTENTSTABLE"};
            assert_true(order == want_order, "the classes are in AutoCAD's order");
        }
    }
}

TEST(order_index_layer_names_follow_the_tables_spelling_in_r14_and_are_checked) {
    // R13/R14 name a layer as its record is spelled (the writer's rule for every layer name there)
    {
        Drawing d(DxfVersion::R14);
        d.layers().push_back({});
        d.layers().back().name = "MyLayer";
        hdxf::IdBufferData b;
        d.objects().push_back(make_object(ObjectType::IdBuffer, "7B03", b));
        hdxf::LayerIndexData index;
        index.entries = {{"mylayer", "7B03", 0}};
        d.objects().push_back(make_object(ObjectType::LayerIndex, "7B05", index));
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "R14");
        Drawing r = load(out, "R14");
        const Object* o = find_by_type_and_index(r, ObjectType::LayerIndex, 0);
        assert_true(o != nullptr, "the index");
        assert_eq(std::string("MyLayer"), o->as_layer_index().entries[0].layer_name, "the spelling of the layer's record");
    }
    // a name R13/R14 cannot hold: a named error before anything is written
    {
        Drawing d(DxfVersion::R14);
        hdxf::IdBufferData b;
        d.objects().push_back(make_object(ObjectType::IdBuffer, "7B03", b));
        hdxf::LayerIndexData index;
        index.entries = {{"A layer name with characters R14 does not allow: <>?*|", "7B03", 0}};
        d.objects().push_back(make_object(ObjectType::LayerIndex, "7B05", index));
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(out.empty(), "nothing is written");
        assert_true(d.last_error().find("Layer") != std::string::npos, d.last_error());
    }
}

TEST(order_index_utf8_and_long_names_in_every_version) {
    const std::string long_name(300, 'n');
    const std::string unicode = "Lager \xC3\xA5\xC3\xA4\xC3\xB6 \xE2\x88\x85 \xF0\x9F\x98\x80";     // åäö ∅ and a character above U+FFFF
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2018}) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            Drawing d(v);
            hdxf::LayerFilterData lf;
            lf.layers = {unicode, long_name};
            d.objects().push_back(make_object(ObjectType::LayerFilter, "7B08", lf));
            std::vector<uint8_t> out = save(d, f, hdxf_test::version_label(v));
            Drawing r = load(out, hdxf_test::version_label(v));
            const Object* o = find_by_type_and_index(r, ObjectType::LayerFilter, 0);
            assert_true(o != nullptr, "the filter");
            assert_eq(unicode, o->as_layer_filter().layers[0], std::string(hdxf_test::version_label(v)) + " " + hdxf_test::format_label(f) + ": UTF-8");
            assert_eq(long_name, o->as_layer_filter().layers[1], "a long name");
        }
    }
}

// ---------------------------------------------------------------------------
// Hostile and unusual input (B105): grammar-driven reading, a record that cannot be read stays as it was
// ---------------------------------------------------------------------------

namespace {

// The drawing with `record` in its OBJECTS: loaded, the record found, written again and the record still there
struct Hostile {
    Drawing d;
    const Object* object = nullptr;
};

Hostile load_with(const Rec& record, const std::string& type_name) {
    std::string text = drawing_with_objects({record});
    Hostile h;
    assert_true(h.d.load_from_memory(reinterpret_cast<const uint8_t*>(text.data()), text.size()), h.d.last_error());
    for (const Object& o : h.d.objects()) {
        if (o.handle == record[1].second) h.object = &o;
    }
    assert_true(h.object != nullptr, type_name + " is in the drawing");
    return h;
}

bool has_warning(const Drawing& d, const std::string& part) {
    return std::any_of(d.warnings().begin(), d.warnings().end(), [&](const std::string& w) { return w.find(part) != std::string::npos; });
}

// The record stayed untyped and is written back with the groups it had
void expect_kept_untyped(const Rec& record, const std::string& type_name, const std::string& warning_part) {
    Hostile h = load_with(record, type_name);
    assert_true(h.object->type == ObjectType::Unknown, type_name + ": not typed");
    assert_true(has_warning(h.d, warning_part), type_name + ": a warning with \"" + warning_part + "\"");
    assert_true(has_warning(h.d, "kept as it was read"), "the warning says it is kept");
    std::vector<uint8_t> out = h.d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), h.d.last_error());
    std::string text = hdxf_test::lf_text(out);
    // every group of the record is in the file again
    std::string wanted = record_text(record);
    assert_true(text.find(wanted) != std::string::npos, type_name + ": the record is written back as it was");
}

} // namespace

TEST(order_index_a_record_that_cannot_be_read_stays_as_it_was_with_a_warning) {
    // 331 without the sort handle that must follow
    expect_kept_untyped(sortents("7F01", {{330, "1F"}, {331, "7F10"}}), "SORTENTSTABLE", "sort handle");
    // a number that is not a number
    expect_kept_untyped(Rec{{0, "LAYER_INDEX"}, {5, "7F02"}, {330, "0"}, {100, "AcDbIndex"}, {40, "not a date"}, {100, "AcDbLayerIndex"}},
                        "LAYER_INDEX", "not a number");
    // an entry without its count
    expect_kept_untyped(Rec{{0, "LAYER_INDEX"}, {5, "7F03"}, {330, "0"}, {100, "AcDbIndex"}, {40, "2461000.5"},
                            {100, "AcDbLayerIndex"}, {8, "WALLS"}, {360, "7F20"}}, "LAYER_INDEX", "90");
    // a boundary count that is more than the groups that remain: checked before anything is allocated
    expect_kept_untyped(Rec{{0, "SPATIAL_FILTER"}, {5, "7F04"}, {330, "0"}, {100, "AcDbFilter"}, {100, "AcDbSpatialFilter"},
                            {70, "2000000000"}, {10, "1.0"}, {20, "2.0"}}, "SPATIAL_FILTER", "more than the");
    // a boundary count of 2 with one point
    expect_kept_untyped(Rec{{0, "SPATIAL_FILTER"}, {5, "7F05"}, {330, "0"}, {100, "AcDbFilter"}, {100, "AcDbSpatialFilter"},
                            {70, "2"}, {10, "1.0"}, {20, "2.0"}, {210, "0"}, {220, "0"}, {230, "1"}}, "SPATIAL_FILTER", "boundary point");
    // a spatial filter with 23 of the 24 matrix numbers
    {
        Rec r = {{0, "SPATIAL_FILTER"}, {5, "7F06"}, {330, "0"}, {100, "AcDbFilter"}, {100, "AcDbSpatialFilter"}, {70, "2"},
                 {10, "0"}, {20, "0"}, {10, "5"}, {20, "5"}, {210, "0"}, {220, "0"}, {230, "1"}, {11, "0"}, {21, "0"}, {31, "0"},
                 {71, "1"}, {72, "0"}, {73, "0"}};
        for (int i = 0; i < 23; ++i) r.push_back({40, i % 5 == 0 ? "1.0" : "0.0"});
        expect_kept_untyped(r, "SPATIAL_FILTER", "group 40");
    }
    // a front plane flag with no distance
    {
        Rec r = {{0, "SPATIAL_FILTER"}, {5, "7F07"}, {330, "0"}, {100, "AcDbFilter"}, {100, "AcDbSpatialFilter"}, {70, "0"},
                 {210, "0"}, {220, "0"}, {230, "1"}, {11, "0"}, {21, "0"}, {31, "0"}, {71, "1"}, {72, "1"}, {73, "0"}};
        expect_kept_untyped(r, "SPATIAL_FILTER", "group 40");
    }
    // the tree of a spatial index: a length that is not the bytes', an odd number of hex digits, no end marker
    expect_kept_untyped(Rec{{0, "SPATIAL_INDEX"}, {5, "7F08"}, {330, "0"}, {100, "AcDbIndex"}, {40, "2461000.5"}, {100, "AcDbSpatialIndex"},
                            {40, "1"}, {40, "2"}, {40, "3"}, {40, "4"}, {40, "5"}, {40, "6"}, {90, "0"}, {90, "10"}, {310, "0102AA"},
                            {1, "END ACDBSPATIALINDEX BINARY DATA"}}, "SPATIAL_INDEX", "length");
    expect_kept_untyped(Rec{{0, "SPATIAL_INDEX"}, {5, "7F09"}, {330, "0"}, {100, "AcDbIndex"}, {40, "2461000.5"}, {100, "AcDbSpatialIndex"},
                            {40, "1"}, {40, "2"}, {40, "3"}, {40, "4"}, {40, "5"}, {40, "6"}, {90, "0"}, {90, "3"}, {310, "0102A"},
                            {1, "END ACDBSPATIALINDEX BINARY DATA"}}, "SPATIAL_INDEX", "odd number of hex");
    expect_kept_untyped(Rec{{0, "SPATIAL_INDEX"}, {5, "7F0A"}, {330, "0"}, {100, "AcDbIndex"}, {40, "2461000.5"}, {100, "AcDbSpatialIndex"},
                            {40, "1"}, {40, "2"}, {40, "3"}, {40, "4"}, {40, "5"}, {40, "6"}, {90, "0"}, {90, "3"}, {310, "0102AA"}},
                        "SPATIAL_INDEX", "group 1");
    // a missing subclass
    expect_kept_untyped(Rec{{0, "IDBUFFER"}, {5, "7F0B"}, {330, "0"}}, "IDBUFFER", "AcDbIdBuffer");
    expect_kept_untyped(Rec{{0, "LAYER_FILTER"}, {5, "7F0C"}, {330, "0"}, {100, "AcDbFilter"}}, "LAYER_FILTER", "AcDbLayerFilter");
}

TEST(order_index_groups_after_the_known_ones_are_kept_with_a_warning_for_a_later_version) {
    // IDBUFFER with a group a later version might add after its members; the record is typed and written as it was
    Rec r = {{0, "IDBUFFER"}, {5, "7F20"}, {330, "0"}, {100, "AcDbIdBuffer"}, {330, "1F"}, {330, "7F21"}, {70, "5"}, {1, "future"}};
    Hostile h = load_with(r, "IDBUFFER");
    assert_true(h.object->type == ObjectType::IdBuffer, "typed");
    assert_eq(2, static_cast<int>(h.object->as_id_buffer().object_handles.size()));
    assert_true(has_warning(h.d, "after the ones HDxf knows"), "the warning");
    std::vector<uint8_t> out = h.d.save_to_memory(DxfFormat::Ascii);
    assert_true(hdxf_test::lf_text(out).find(record_text(r)) != std::string::npos, "written as it was");
    // changed: the future group stays where it was after the members
    Drawing d;
    std::string text = drawing_with_objects({r});
    assert_true(d.load_from_memory(reinterpret_cast<const uint8_t*>(text.data()), text.size()), d.last_error());
    Object* o = d.find_object("7F20");
    o->as_id_buffer().object_handles.push_back("1F");
    std::vector<uint8_t> changed = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!changed.empty(), d.last_error());
    std::string changed_text = hdxf_test::lf_text(changed);
    assert_true(changed_text.find("  1\nfuture\n") != std::string::npos, "the future group is kept when a member is added");
}

TEST(order_index_a_hostile_boundary_count_and_huge_numbers_do_not_allocate) {
    // 70 = 2^31 - 1 with a handful of groups after it: refused before anything is made
    Rec r = {{0, "SPATIAL_FILTER"}, {5, "7F30"}, {330, "0"}, {100, "AcDbFilter"}, {100, "AcDbSpatialFilter"}, {70, "2147483647"}};
    for (int i = 0; i < 6; ++i) { r.push_back({10, "1"}); r.push_back({20, "2"}); }
    Hostile h = load_with(r, "SPATIAL_FILTER");
    assert_true(h.object->type == ObjectType::Unknown, "not typed");
    // a count above the range of an int
    Rec s = {{0, "SPATIAL_FILTER"}, {5, "7F31"}, {330, "0"}, {100, "AcDbFilter"}, {100, "AcDbSpatialFilter"}, {70, "99999999999999999999"}};
    Hostile h2 = load_with(s, "SPATIAL_FILTER");
    assert_true(h2.object->type == ObjectType::Unknown, "not typed");
    // a tree length above int32
    Rec t = {{0, "SPATIAL_INDEX"}, {5, "7F32"}, {330, "0"}, {100, "AcDbIndex"}, {40, "1"}, {100, "AcDbSpatialIndex"},
             {40, "1"}, {40, "2"}, {40, "3"}, {40, "4"}, {40, "5"}, {40, "6"}, {90, "0"}, {90, "4294967295"}, {1, "END ACDBSPATIALINDEX BINARY DATA"}};
    Hostile h3 = load_with(t, "SPATIAL_INDEX");
    assert_true(h3.object->type == ObjectType::Unknown, "not typed");
}

TEST(order_index_a_record_without_the_optional_groups_is_read_and_written_without_them) {
    // SORTENTSTABLE with no block record and no pairs, a spatial filter with no points, an empty layer index
    Rec a = sortents("7F40", {});
    a.erase(a.begin() + 2);         // no owner group either: a different record layout from AutoCAD's, still valid
    Rec b = {{0, "SPATIAL_FILTER"}, {5, "7F41"}, {330, "0"}, {100, "AcDbFilter"}, {100, "AcDbSpatialFilter"}, {70, "0"},
             {210, "0"}, {220, "0"}, {230, "1"}, {11, "0"}, {21, "0"}, {31, "0"}, {71, "0"}, {72, "0"}, {73, "0"}};
    for (int i = 0; i < 24; ++i) b.push_back({40, i % 5 == 0 ? "1.0" : "0.0"});
    Rec c = {{0, "LAYER_INDEX"}, {5, "7F42"}, {330, "0"}, {100, "AcDbIndex"}, {40, "2461000.5"}, {100, "AcDbLayerIndex"}};
    std::string text = drawing_with_objects({a, b, c});
    Drawing d;
    assert_true(d.load_from_memory(reinterpret_cast<const uint8_t*>(text.data()), text.size()), d.last_error());
    assert_true(d.find_object("7F40")->type == ObjectType::SortentsTable, "SORTENTSTABLE");
    assert_true(d.find_object("7F40")->as_sortents_table().entries.empty() && d.find_object("7F40")->as_sortents_table().block_record_handle.empty(), "empty");
    assert_true(d.find_object("7F41")->type == ObjectType::SpatialFilter && d.find_object("7F41")->as_spatial_filter().boundary.empty(), "no points");
    assert_false(d.find_object("7F41")->as_spatial_filter().enabled, "71 0");
    assert_true(d.find_object("7F42")->type == ObjectType::LayerIndex && d.find_object("7F42")->as_layer_index().entries.empty(), "an empty index");
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), d.last_error());
    // from the typed values alone
    for (Object& o : d.objects()) if (is_order_index_type(o.type)) o.raw = hdxf::RawRecord();
    std::vector<uint8_t> again = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!again.empty(), d.last_error());
    Drawing r = load(again, "again");
    assert_true(r.find_object("7F40")->as_sortents_table().block_record_handle.empty(), "a block record that was not there is not made");
    assert_eq(0, static_cast<int>(r.find_object("7F41")->as_spatial_filter().boundary.size()));
}

TEST(order_index_a_pointer_to_something_that_is_gone_becomes_zero_and_a_sort_key_is_never_one) {
    // The descriptors (B104): the pair's 331 is a pointer that takes its 5 with it, the 5 is an ordering key, the block record
    // (330 in the subclass) a pointer that becomes 0
    const RefDescriptor* pair = find_reference("SORTENTSTABLE", "AcDbSortentsTable", 0, 331, 3);
    assert_true(pair && pair->removal == RefRemoval::DropPair && pair->kind == RefKind::SoftPointer && pair->last_offset == 1 && pair->partner_code == 5,
                "331: the pair goes");
    const RefDescriptor* key = find_reference("SORTENTSTABLE", "AcDbSortentsTable", 0, 5, 3);
    assert_true(key && key->kind == RefKind::SortKey && key->removal == RefRemoval::Keep, "5: a sort key, never touched");
    const RefDescriptor* record = find_reference("SORTENTSTABLE", "AcDbSortentsTable", 0, 330, 0);
    assert_true(record && record->kind == RefKind::SoftPointer && record->removal == RefRemoval::ZeroInPlace, "330: the block record");
    assert_true(find_reference("SORTENTSTABLE", "AcDbSortentsTable", 0, 330, 1) == nullptr, "only the first 330");
    const RefDescriptor* member = find_reference("IDBUFFER", "AcDbIdBuffer", 0, 330, 2);
    assert_true(member && member->removal == RefRemoval::DropMember, "an IDBUFFER member goes");
    const RefDescriptor* owner = find_reference("LAYER_INDEX", "AcDbLayerIndex", 0, 360, 1);
    assert_true(owner && owner->kind == RefKind::HardOwner && owner->removal == RefRemoval::DropPair && owner->first_offset == -1 &&
                    owner->last_offset == 1, "an entry (8 360 90) goes whole");
    assert_true(find_reference("LAYER_INDEX", "AcDbLayerIndex", 0, 8, 1) == nullptr, "a layer name is no pointer");
    assert_true(find_reference("SPATIAL_FILTER", "AcDbSpatialFilter", 0, 40, 0) == nullptr, "a clip has no pointers");

    // In a drawing read from a file: the block record pointer is zeroed when what it points at is gone
    std::vector<uint8_t> in = facit_bytes(order_index_file("2018", false));
    Drawing d = load(in, "2018");
    std::string insert;
    for (const hdxf::Entity& e : d.entities()) {
        if (e.type == hdxf::EntityType::Insert) { insert = e.props.handle; break; }
    }
    objects_mut(d, ObjectType::SortentsTable)[1]->as_sortents_table().block_record_handle = insert;
    remove_entity(d, insert);
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "pointer");
    Drawing r = load(out, "pointer");
    assert_eq(std::string("0"), objects_of(r, ObjectType::SortentsTable)[1]->as_sortents_table().block_record_handle,
              "a pointer to a record that is gone is 0 (none)");
    assert_true(has_warning_text(d, "no longer exists, set to none"), "and the save says so");
}
