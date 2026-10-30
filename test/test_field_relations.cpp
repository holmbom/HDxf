#include "test_framework.hpp"
#include "hdxf.hpp"
#include "typed_corpus.hpp"
#include "TypedRecordTypes.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Step 9.8 of phase 9 (plan 2026-10-27-fas-9-typer.md), B19/B104/B108: what a field is related to — its child fields
// (hard owners, 360), the objects its code names (331, soft pointers), the object values of its data (330), the
// FIELDLIST that lists it (330), and the data tables' object values (331, 330, 340, 350, 360). A record that is
// gone leaves the list it was a member of and the count follows; a pointer that stands alone is a 0 in its place; a
// link the file had broken already stays as it was.
//
// The originals: doc/facit/acad_<version>_dxf_fields*.dxf (fas9_document_data.scr: TEXT with fields, then the same drawing
// with the fields updated and with the last text erased), acad_<version>_dxf_tables*.dxf (Erik's GUI tables: fields that name
// an object) and acad_<version>_dxf_table_empty.dxf (the round-trip data tables). Local files: a test whose file is missing is skipped.
// ---------------------------------------------------------------------------

using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::Object;
using hdxf::ObjectType;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

namespace {

std::filesystem::path facit_path(const std::string& name) { return hdxf_test::source_path("doc/facit/" + name); }

std::vector<uint8_t> facit_bytes(const std::string& name) {
    std::vector<uint8_t> in = hdxf_test::read_file_bytes(facit_path(name));
    if (in.empty()) hdxf_test::skip("missing doc/facit/" + name);
    return in;
}

Drawing load(const std::vector<uint8_t>& bytes) {
    Drawing d;
    assert_true(d.load_from_memory(bytes.data(), bytes.size()), d.last_error());
    return d;
}

std::vector<uint8_t> save(Drawing& d) {
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), d.last_error());
    return out;
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

void remove_entity(Drawing& d, const std::string& handle) {
    auto& list = d.entities();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const hdxf::Entity& e) { return upper(e.props.handle) == upper(handle); }), list.end());
}

void remove_object(Drawing& d, const std::string& handle) {
    auto& list = d.objects();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Object& o) { return upper(o.handle) == upper(handle); }), list.end());
}

const Object* field_list(const Drawing& d) {
    for (const Object& o : d.objects()) if (o.type == ObjectType::FieldList) return &o;
    return nullptr;
}

// The raw 90 of the FIELDLIST as the file has it
int declared_count(const std::vector<uint8_t>& bytes) {
    std::string text(bytes.begin(), bytes.end());
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    size_t at = text.find("AcDbIdSet\n 90\n");
    assert_true(at != std::string::npos, "the FIELDLIST");
    return std::atoi(text.c_str() + at + 14);
}

// The entity that holds `field` (a TEXT whose extension dictionary leads to it): its handle
std::string host_of(const Drawing& d, const std::string& field) {
    const Object* f = d.find_object(field);
    if (!f) return {};
    // the field is owned by the dictionary the host's extension dictionary names ACAD_FIELD
    const Object* d1 = d.find_object(f->owner);
    const Object* xdict = d1 ? d.find_object(d1->owner) : nullptr;
    return xdict ? xdict->owner : std::string();
}

} // namespace

TEST(field_relations_a_text_removed_takes_its_fields_with_it_and_they_leave_the_field_list) {
    for (const char* tag : {"2000", "2004", "2007", "2010", "2013", "2018"}) {
        std::vector<uint8_t> in = facit_bytes(std::string("acad_") + tag + "_dxf_fields.dxf");
        Drawing d = load(in);
        // the text of two fields
        std::string parent;
        std::vector<std::string> children;
        for (const Object& o : d.objects()) {
            if (o.type == ObjectType::Field && o.as_field().code.rfind("Both:", 0) == 0) {
                parent = o.handle;
                children = o.as_field().child_fields;
            }
        }
        assert_true(!parent.empty() && children.size() == 2, std::string(tag) + ": the parent field with its two children");
        const std::string host = host_of(d, parent);
        assert_true(!host.empty() && d.find_entity(host) != nullptr, "its TEXT");
        const size_t before = field_list(d)->as_field_list().fields.size();
        assert_eq(11, static_cast<int>(before), "eleven in the list");
        remove_entity(d, host);
        std::vector<uint8_t> out = save(d);
        Drawing r = load(out);
        assert_true(r.find_object(parent) == nullptr, "the parent field goes with its text (the extension dictionary owns it)");
        for (const std::string& c : children) assert_true(r.find_object(c) == nullptr, "and its children with it");
        const Object* list = field_list(r);
        assert_true(list != nullptr, "the list stays");
        const auto& members = list->as_field_list().fields;
        assert_eq(static_cast<int>(before) - 3, static_cast<int>(members.size()), "three fewer in the list");
        assert_eq(static_cast<int>(members.size()), declared_count(out), "and 90 follows");
        for (const std::string& m : members) assert_true(r.find_object(m) != nullptr, "every member of the list is a field that is there");
        assert_true(r.warnings().empty(), "no warning");
        // the other fields are the records they were
        std::set<std::string> gone = {parent, children[0], children[1]};
        for (const Object& o : d.objects()) {
            if (o.type != ObjectType::Field || gone.count(o.handle)) continue;
            const Object* now = r.find_object(o.handle);
            assert_true(now != nullptr && now->type == ObjectType::Field, "another field stays");
        }
    }
}

TEST(field_relations_a_link_the_file_had_broken_stays_and_a_new_removal_follows_it) {
    // AutoCAD erased the last text: its FIELDLIST still lists the three fields of it, which are not in the file
    std::vector<uint8_t> in = facit_bytes("acad_2018_dxf_fields_erased.dxf");
    Drawing d = load(in);
    const Object* list = field_list(d);
    assert_eq(11, static_cast<int>(list->as_field_list().fields.size()), "the list as AutoCAD has it");
    int dead = 0;
    for (const std::string& m : list->as_field_list().fields) if (d.find_object(m) == nullptr) ++dead;
    assert_eq(3, dead, "three of its fields are not there");
    // unchanged: the same bytes
    assert_true(save(d) == in, "written unchanged: the broken links stay");
    // another text goes: its two fields leave the list, the three that were dead stay
    std::string parent;
    for (const Object& o : d.objects()) if (o.type == ObjectType::Field && o.as_field().code.rfind("Author:", 0) == 0) parent = o.handle;
    const std::string host = host_of(d, parent);
    assert_true(!host.empty(), "the text of the Author field");
    remove_entity(d, host);
    std::vector<uint8_t> out = save(d);
    Drawing r = load(out);
    const auto& members = r.find_object(list->handle)->as_field_list().fields;
    assert_eq(9, static_cast<int>(members.size()), "the two removed fields left the list, the three broken links stayed");
    assert_eq(9, declared_count(out), "and 90 says nine");
    int still_dead = 0;
    for (const std::string& m : members) if (r.find_object(m) == nullptr) ++still_dead;
    assert_eq(3, still_dead, "the same three broken links");
}

TEST(field_relations_a_child_field_removed_leaves_its_parent_and_the_list_and_the_counts_follow) {
    std::vector<uint8_t> in = facit_bytes("acad_2018_dxf_fields.dxf");
    Drawing d = load(in);
    std::string parent, child;
    for (const Object& o : d.objects()) {
        if (o.type == ObjectType::Field && o.as_field().code.rfind("Author:", 0) == 0) {
            parent = o.handle;
            child = o.as_field().child_fields[0];
        }
    }
    remove_object(d, child);
    std::vector<uint8_t> out = save(d);
    Drawing r = load(out);
    assert_true(r.find_object(child) == nullptr, "the child is gone");
    const hdxf::FieldData& p = r.find_object(parent)->as_field();
    assert_true(p.child_fields.empty(), "the parent no longer lists it (360)");
    // the parent's code is as it was: its `%<\_FldIdx 0>%` is the parent's own text (no field was made up for it)
    assert_eq(std::string("Author: %<\\_FldIdx 0>%"), p.code, "the code");
    // the text of a text field is its own, the field list lost one, and the other fields are as they were
    assert_eq(10, static_cast<int>(field_list(r)->as_field_list().fields.size()), "ten in the list");
    assert_eq(10, declared_count(out), "90");
}

// The code refers to the children by position (%<\_FldIdx n>%): the last child can go, one before another cannot, as its
// place would be taken by the next (Codex 9.S, 4). AutoCAD's FIELD 273 of acad_2018_dxf_fields: "Both: … 0 … and … 1"
TEST(field_relations_a_child_field_before_another_cannot_go_without_moving_the_next_into_its_place) {
    std::vector<uint8_t> in = facit_bytes("acad_2018_dxf_fields.dxf");
    std::string parent;
    std::vector<std::string> children;
    {
        Drawing d = load(in);
        for (const Object& o : d.objects()) {
            if (o.type == ObjectType::Field && o.as_field().child_fields.size() == 2 &&
                o.as_field().code.find("_FldIdx 1") != std::string::npos) {
                parent = o.handle;
                children = o.as_field().child_fields;
            }
        }
    }
    assert_true(!parent.empty(), "a field with two children");
    // the first: refused, nothing written
    Drawing first = load(in);
    remove_object(first, children[0]);
    assert_true(first.save_to_memory(hdxf::DxfFormat::Ascii).empty(), "saved although the second child would take the first's place");
    assert_true(first.last_error().find("FIELD " + parent + ": its child field 0 is gone") == 0, first.last_error());
    // the last: written, the first stays in its place
    Drawing last = load(in);
    remove_object(last, children[1]);
    Drawing r = load(save(last));
    const hdxf::FieldData& p = r.find_object(parent)->as_field();
    assert_eq(1, static_cast<int>(p.child_fields.size()), "one child");
    assert_eq(children[0], p.child_fields[0], "the first, in its place");
}

TEST(field_relations_an_object_a_field_names_removed_leaves_its_object_list_and_the_value_becomes_zero) {
    for (const char* tag : {"2000", "2007", "2018"}) {
        std::vector<uint8_t> in = facit_bytes(std::string("acad_") + tag + "_dxf_tables.dxf");
        Drawing d = load(in);
        std::string field, target;
        for (const Object& o : d.objects()) {
            if (o.type == ObjectType::Field && !o.as_field().objects.empty() && field.empty()) {
                field = o.handle;
                target = o.as_field().objects[0];
            }
        }
        assert_true(!field.empty(), std::string(tag) + ": a field that names an object");
        assert_true(d.find_entity(target) != nullptr, "the object is an entity of the drawing");
        remove_entity(d, target);
        std::vector<uint8_t> out = save(d);
        Drawing r = load(out);
        const hdxf::FieldData& f = r.find_object(field)->as_field();
        assert_true(f.objects.empty(), "the object left the field's list (331) and 97 follows");
        bool zero_value = false;
        for (const hdxf::FieldKeyValue& kv : f.data) {
            if (kv.value.type == hdxf::TableValueType::ObjectId) {
                zero_value = std::get<hdxf::TableHandle>(kv.value.value).handle == "0";
            }
        }
        assert_true(zero_value, "the object value of the data (ObjectPropertyId, 330) is 0 in its place");
        // the other fields are as they were: the same code and cache
        for (const Object& o : d.objects()) {
            if (o.type != ObjectType::Field || o.handle == field) continue;
            const Object* now = r.find_object(o.handle);
            assert_true(now != nullptr, "another field is there");
            if (o.as_field().objects.empty()) assert_eq(o.as_field().code, now->as_field().code, "and as it was");
        }
    }
}

TEST(field_relations_a_removed_table_leaves_nothing_of_its_fields_in_the_list_but_the_links_the_file_had_broken) {
    std::vector<uint8_t> in = facit_bytes("acad_2018_dxf_tables.dxf");
    Drawing d = load(in);
    const Object* list = field_list(d);
    const std::vector<std::string> before = list->as_field_list().fields;
    std::set<std::string> dead_in_file;
    for (const std::string& m : before) if (d.find_object(m) == nullptr) dead_in_file.insert(upper(m));
    assert_eq(2, static_cast<int>(dead_in_file.size()), "AutoCAD's own list has two links that lead nowhere");
    // every table of the drawing goes
    auto& entities = d.entities();
    entities.erase(std::remove_if(entities.begin(), entities.end(), [](const hdxf::Entity& e) { return e.type == hdxf::EntityType::Table; }), entities.end());
    std::vector<uint8_t> out = save(d);
    Drawing r = load(out);
    const auto& members = r.find_object(list->handle)->as_field_list().fields;
    for (const std::string& m : members) {
        assert_true(r.find_object(m) != nullptr || dead_in_file.count(upper(m)) > 0, "a member is a field that is there or a link the file had broken: " + m);
    }
    assert_eq(static_cast<int>(members.size()), declared_count(out), "90 follows");
    assert_true(members.size() < before.size(), "the table's fields left");
    // the fields the tables' content owned (TABLECONTENT 890 owns 880, 882 and 884, and they their children) are gone;
    // the fields of the MTEXT stay
    for (const char* h : {"880", "881", "882", "883", "884", "885"}) {
        assert_true(r.find_object(h) == nullptr, std::string("field ") + h + " went with its table's content");
    }
    int left = 0;
    for (const Object& o : r.objects()) if (o.type == ObjectType::Field) ++left;
    assert_true(left > 0, "the fields of the text that is not a table stay");
}

TEST(field_relations_a_removed_cell_record_is_a_zero_in_the_round_trip_data_table_and_the_table_keeps_the_rest) {
    for (const char* tag : {"2000", "2004"}) {
        std::vector<uint8_t> in = facit_bytes(std::string("acad_") + tag + "_dxf_table_empty.dxf");
        Drawing d = load(in);
        const hdxf::Entity* table = nullptr;
        for (const auto& e : d.entities()) if (e.type == hdxf::EntityType::Table && !table) table = &e;
        const Object* data = d.table_round_trip_data(*table);
        assert_true(data != nullptr, "the data table");
        const std::string data_handle = data->handle;
        const hdxf::DataColumn& owned = data->as_data_table().columns[3];
        const std::string cell = std::get<hdxf::DataTableObject>(owned.values[5]).handle;
        remove_object(d, cell);
        std::vector<uint8_t> out = save(d);
        Drawing r = load(out);
        const hdxf::DataTableData& t = r.find_object(data_handle)->as_data_table();
        assert_eq(std::string("0"), std::get<hdxf::DataTableObject>(t.columns[3].values[5]).handle, "the removed record is a 0 in its place (a row is not taken out of one column)");
        assert_eq(static_cast<int>(owned.values.size()), static_cast<int>(t.columns[3].values.size()), "the column keeps its length");
        assert_eq(std::get<hdxf::DataTableObject>(owned.values[6]).handle, std::get<hdxf::DataTableObject>(t.columns[3].values[6]).handle, "the next one is as it was");
    }
}

TEST(field_relations_every_kind_of_object_value_of_a_data_table_is_its_own_pointer_kind_and_goes_to_zero) {
    // The reference lists five groups for an object in a DATATABLE column: 331, 330, 340, 350 and 360. A target that is
    // gone is a 0 in its place for each of them; the others stay
    hdxf::Drawing d(hdxf::DxfVersion::R2004);
    hdxf::Entity& line = d.add_line({0, 0, 0}, {1, 1, 0});
    line.props.handle = "A01";
    const std::vector<std::pair<hdxf::DataTableObjectKind, std::string>> kinds = {
        {hdxf::DataTableObjectKind::ObjectValue, "A02"}, {hdxf::DataTableObjectKind::SoftPointer, "A03"},
        {hdxf::DataTableObjectKind::HardPointer, "A04"}, {hdxf::DataTableObjectKind::SoftOwner, "A05"},
        {hdxf::DataTableObjectKind::HardOwner, "A06"}};
    hdxf::DataTableData t;
    t.name = "pointers";
    int32_t type = 20;
    for (const auto& [kind, handle] : kinds) {
        Object x;
        x.type = ObjectType::XRecord;
        x.handle = handle;
        x.owner = kind == hdxf::DataTableObjectKind::HardOwner ? std::string("A07") : std::string("0");
        x.data = hdxf::XRecordData{};
        d.objects().push_back(x);
        hdxf::DataColumn c;
        c.type = type++;                    // a type number HDxf has not seen: the value's own group decides
        c.name = "k" + std::to_string(static_cast<int>(kind));
        c.values = {hdxf::DataTableValue(hdxf::DataTableObject{kind, handle}), hdxf::DataTableValue(hdxf::DataTableObject{kind, "A01"})};
        t.columns.push_back(c);
    }
    t.rows = 2;
    Object& table = d.add_data_table(t, "0");
    table.handle = "A07";
    std::vector<uint8_t> bytes = save(d);
    Drawing r = load(bytes);
    const hdxf::DataTableData& back = r.find_object("A07")->as_data_table();
    for (size_t i = 0; i < kinds.size(); ++i) {
        assert_true(std::get<hdxf::DataTableObject>(back.columns[i].values[0]).kind == kinds[i].first, "the pointer kind is the group's");
    }
    // the five records and the entity go
    for (const auto& [kind, handle] : kinds) {
        if (kind != hdxf::DataTableObjectKind::HardOwner) remove_object(r, handle);
    }
    remove_entity(r, "A01");
    std::vector<uint8_t> out = save(r);
    Drawing after = load(out);
    const hdxf::DataTableData& zeroed = after.find_object("A07")->as_data_table();
    for (size_t i = 0; i < kinds.size(); ++i) {
        const std::string handle0 = std::get<hdxf::DataTableObject>(zeroed.columns[i].values[0]).handle;
        const std::string handle1 = std::get<hdxf::DataTableObject>(zeroed.columns[i].values[1]).handle;
        if (kinds[i].first == hdxf::DataTableObjectKind::HardOwner) {
            // owned by the table: it stays (its owner is the table)
            assert_eq(kinds[i].second, handle0, "a hard-owned record the table owns stays");
        } else {
            assert_eq(std::string("0"), handle0, "the removed record is a 0 in its place (group " + std::to_string(static_cast<int>(kinds[i].first)) + ")");
        }
        assert_eq(std::string("0"), handle1, "the removed entity is a 0 in its place");
        assert_eq(2, static_cast<int>(zeroed.columns[i].values.size()), "the rows stay");
    }
}

TEST(field_relations_the_xrecord_that_owns_a_data_table_takes_it_and_its_cells_with_it) {
    for (const char* tag : {"2000", "2004"}) {
        std::vector<uint8_t> in = facit_bytes(std::string("acad_") + tag + "_dxf_table_empty.dxf");
        Drawing d = load(in);
        const hdxf::Entity* table = nullptr;
        for (const auto& e : d.entities()) if (e.type == hdxf::EntityType::Table && !table) table = &e;
        const Object* data = d.table_round_trip_data(*table);
        const std::string owner = data->owner;
        std::vector<std::string> cells;
        for (const hdxf::DataTableValue& v : data->as_data_table().columns[3].values) cells.push_back(std::get<hdxf::DataTableObject>(v).handle);
        const std::string data_handle = data->handle;
        const size_t objects_before = d.objects().size();
        remove_object(d, owner);
        Drawing r = load(save(d));
        assert_true(r.find_object(data_handle) == nullptr, "the data table goes with its XRECORD (its owner)");
        for (const std::string& c : cells) assert_true(r.find_object(c) == nullptr, "and its cells with it");
        assert_eq(static_cast<int>(objects_before) - static_cast<int>(cells.size()) - 2, static_cast<int>(r.objects().size()), "nothing else went");
    }
}
