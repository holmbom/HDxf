#include "test_framework.hpp"
#include "test_drawing_builder.hpp"
#include "hdxf.hpp"
#include <filesystem>
#include <string>

// ---------------------------------------------------------------------------
// OBJECTS and BLOCK_RECORD (phase 1, step 1.1 — doc/plans/2026-10-23-fas-1-dxf-2000.md)
//
// Expected values are read from the files themselves (ACadSharp's AutoCAD samples), not
// from HDxf.
// ---------------------------------------------------------------------------

namespace {

std::filesystem::path external(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(name);
}

bool load_external(hdxf::Drawing& dwg, const std::string& name) {
    if (!std::filesystem::exists(external(name))) {
        hdxf_test::skip("external test file missing: " + name);
        return false;
    }
    hdxf_test::assert_true(dwg.load(external(name)), name + ": " + dwg.last_error());
    return true;
}

int count_type(const hdxf::Drawing& dwg, hdxf::ObjectType type) {
    int n = 0;
    for (const auto& o : dwg.objects()) n += o.type == type ? 1 : 0;
    return n;
}

int count_unknown(const hdxf::Drawing& dwg, const std::string& name) {
    int n = 0;
    for (const auto& o : dwg.objects()) {
        if (o.type == hdxf::ObjectType::Unknown && o.as_unknown().object_type == name) ++n;
    }
    return n;
}

const hdxf::Object& object(const hdxf::Drawing& dwg, const std::string& handle) {
    const hdxf::Object* o = dwg.find_object(handle);
    hdxf_test::assert_true(o != nullptr, "object " + handle + " not found");
    return *o;
}

const hdxf::Block& block(const hdxf::Drawing& dwg, const std::string& name) {
    const hdxf::Block* b = dwg.find_block(name);
    hdxf_test::assert_true(b != nullptr, "block " + name + " not found");
    return *b;
}

// The layouts of the ACadSharp sample (the same in 2000 and 2018): name → LAYOUT handle
// → BLOCK_RECORD handle, and back through BLOCK_RECORD 340
void check_sample_layouts(const hdxf::Drawing& dwg) {
    struct Expected { const char* name; const char* layout; const char* record; const char* block; };
    const Expected expected[] = {
        {"Layout1", "59", "58", "*Paper_Space0"},
        {"Layout2", "5E", "5D", "*Paper_Space1"},
        {"Model", "22", "1F", "*Model_Space"},
        {"MyLayout", "25A", "259", "*Paper_Space"},
    };
    std::vector<const hdxf::Object*> layouts = dwg.layouts();
    hdxf_test::assert_eq(static_cast<int>(layouts.size()), 4, "layouts");
    for (size_t i = 0; i < layouts.size(); ++i) {
        const hdxf::LayoutData& l = layouts[i]->as_layout();
        hdxf_test::assert_eq(l.name, expected[i].name, "layout name");
        hdxf_test::assert_eq(layouts[i]->handle, expected[i].layout, "layout handle");
        hdxf_test::assert_eq(l.block_record_handle, expected[i].record, "LAYOUT 330");
        hdxf_test::assert_eq(layouts[i]->owner, "1A", "LAYOUT owner");
        const hdxf::Block& b = block(dwg, expected[i].block);
        hdxf_test::assert_eq(b.record.handle, expected[i].record, "BLOCK_RECORD handle");
        hdxf_test::assert_eq(b.record.layout_handle, expected[i].layout, "BLOCK_RECORD 340");
    }
}

} // namespace

TEST(objects_dictionary_tree_2000) {
    hdxf::Drawing dwg;
    if (!load_external(dwg, "acadsharp/sample_AC1015_ascii.dxf")) return;

    hdxf_test::assert_eq(static_cast<int>(dwg.objects().size()), 490, "objects");
    hdxf_test::assert_eq(count_type(dwg, hdxf::ObjectType::Dictionary), 134, "DICTIONARY");
    hdxf_test::assert_eq(count_type(dwg, hdxf::ObjectType::XRecord), 218, "XRECORD");
    hdxf_test::assert_eq(count_type(dwg, hdxf::ObjectType::DictionaryVar), 16, "DICTIONARYVAR");
    hdxf_test::assert_eq(count_type(dwg, hdxf::ObjectType::Group), 2, "GROUP");
    hdxf_test::assert_eq(count_type(dwg, hdxf::ObjectType::VisualStyle), 24, "VISUALSTYLE");
    hdxf_test::assert_eq(count_type(dwg, hdxf::ObjectType::Scale), 33, "SCALE");

    // Root dictionary: first object, no owner, 26 entries
    const hdxf::Object& root = dwg.objects().front();
    hdxf_test::assert_eq(root.handle, "C");
    hdxf_test::assert_eq(root.owner, "0");
    const hdxf::DictionaryData& rd = root.as_dictionary();
    hdxf_test::assert_eq(static_cast<int>(rd.entries.size()), 26, "root entries");
    hdxf_test::assert_eq(rd.cloning, 1);
    hdxf_test::assert_eq(rd.entries[0].name, "ACAD_2007HEADERVARS_RT");
    hdxf_test::assert_eq(rd.entries[5].name, "ACAD_GROUP");
    hdxf_test::assert_eq(rd.entries[5].handle, "D");
    hdxf_test::assert_eq(rd.entries[9].name, "ACAD_LAYOUT");
    hdxf_test::assert_eq(rd.entries[9].handle, "1A");
    hdxf_test::assert_false(rd.entries[9].hard_owner, "350 is a soft owner");
    hdxf_test::assert_eq(rd.entries[25].name, "DWGPROPS");

    // ACAD_PLOTSTYLENAME is a dictionary with default → placeholder Normal
    const hdxf::Object& plot_styles = object(dwg, "E");
    hdxf_test::assert_true(plot_styles.type == hdxf::ObjectType::DictionaryWithDefault);
    const auto& pd = plot_styles.as_dictionary_with_default();
    hdxf_test::assert_eq(pd.default_handle, "F");
    hdxf_test::assert_eq(static_cast<int>(pd.dictionary.entries.size()), 1);
    hdxf_test::assert_eq(pd.dictionary.entries[0].name, "Normal");
    const hdxf::Object& normal = object(dwg, "F");
    hdxf_test::assert_true(normal.type == hdxf::ObjectType::Placeholder);
    hdxf_test::assert_eq(normal.owner, "E");
    hdxf_test::assert_eq(static_cast<int>(normal.reactors.size()), 1);
    hdxf_test::assert_eq(normal.reactors[0], "E");

    // Handles are compared as numbers
    hdxf_test::assert_true(dwg.find_object("e") == &plot_styles, "lower case");
    hdxf_test::assert_true(dwg.find_object("000E") == &plot_styles, "leading zeros");
    hdxf_test::assert_true(dwg.find_object("FFFFFF") == nullptr, "missing");
}

TEST(objects_groups_and_variables_2000) {
    hdxf::Drawing dwg;
    if (!load_external(dwg, "acadsharp/sample_AC1015_ascii.dxf")) return;

    const hdxf::DictionaryData& groups = object(dwg, "D").as_dictionary();
    hdxf_test::assert_eq(static_cast<int>(groups.entries.size()), 2);
    hdxf_test::assert_eq(groups.entries[0].name, "*A1");

    const hdxf::Object& a1 = object(dwg, groups.entries[0].handle);
    hdxf_test::assert_true(a1.type == hdxf::ObjectType::Group);
    hdxf_test::assert_eq(a1.owner, "D");
    const hdxf::GroupData& g = a1.as_group();
    hdxf_test::assert_true(g.unnamed, "70 1");
    hdxf_test::assert_true(g.selectable, "71 1");
    hdxf_test::assert_eq(g.description, "");
    const char* members[] = {"69D", "69C", "69B", "69A", "699", "698"};
    hdxf_test::assert_eq(static_cast<int>(g.entities.size()), 6, "members");
    for (int i = 0; i < 6; ++i) hdxf_test::assert_eq(g.entities[i], members[i]);

    const hdxf::Object& dimassoc = object(dwg, "67");
    hdxf_test::assert_true(dimassoc.type == hdxf::ObjectType::DictionaryVar);
    hdxf_test::assert_eq(dimassoc.as_dictionary_var().value, "2");
    hdxf_test::assert_eq(dimassoc.as_dictionary_var().schema, 0);
}

// XRECORD data may hold any code, also 102 and 330, which elsewhere are record structure
TEST(objects_xrecord_data_2000) {
    hdxf::Drawing dwg;
    if (!load_external(dwg, "acadsharp/sample_AC1015_ascii.dxf")) return;

    const hdxf::Object& lt = object(dwg, "151D");
    hdxf_test::assert_true(lt.type == hdxf::ObjectType::XRecord);
    const hdxf::XRecordData& d = lt.as_xrecord();
    hdxf_test::assert_eq(d.cloning, 1);
    hdxf_test::assert_eq(static_cast<int>(d.data.size()), 2);
    hdxf_test::assert_eq(d.data[0].code, 102);
    hdxf_test::assert_eq(d.data[0].value, "ACAD_ROUNDTRIP_2011_LINETYPE");
    hdxf_test::assert_eq(d.data[1].code, 310);

    const hdxf::XRecordData& recompose = object(dwg, "14A0").as_xrecord();
    hdxf_test::assert_eq(static_cast<int>(recompose.data.size()), 12, "90 + 11 × 330");
    hdxf_test::assert_eq(recompose.data[0].code, 90);
    hdxf_test::assert_eq(recompose.data[1].code, 330);
    hdxf_test::assert_eq(recompose.data[1].value, "87");
    hdxf_test::assert_eq(recompose.data[11].value, "A35");
}

TEST(objects_layouts_and_block_records_2000) {
    hdxf::Drawing dwg;
    if (!load_external(dwg, "acadsharp/sample_AC1015_ascii.dxf")) return;
    check_sample_layouts(dwg);

    const hdxf::Object& model = object(dwg, "22");
    const hdxf::LayoutData& m = model.as_layout();
    hdxf_test::assert_eq(m.last_viewport_handle, "94");
    hdxf_test::assert_eq(m.tab_order, 0);
    hdxf_test::assert_eq(m.limits_max.x, 12.0);
    hdxf_test::assert_eq(m.limits_max.y, 9.0);
    hdxf_test::assert_eq(model.xdictionary, "194");
    hdxf_test::assert_eq(m.plot_settings.paper_size, "ANSI_A_(8.50_x_11.00_Inches)");
    hdxf_test::assert_eq(m.plot_settings.printer_name, "none_device");
    hdxf_test::assert_eq(m.plot_settings.flags, 11952);
    hdxf_test::assert_eq(m.plot_settings.scale_denominator, 2.584895464708373);
    hdxf_test::assert_eq(m.plot_settings.scale_factor, 0.3868628397755418);

    const hdxf::LayoutData& l1 = object(dwg, "59").as_layout();
    hdxf_test::assert_eq(l1.tab_order, 1);
    hdxf_test::assert_eq(l1.plot_settings.plot_type, 5);
    hdxf_test::assert_eq(l1.plot_settings.standard_scale_type, 16);
    hdxf_test::assert_eq(l1.limits_min.x, -0.2500002402958907);
    hdxf_test::assert_eq(l1.extents_max.x, 9.449999570846559);
    const auto& xdata = object(dwg, "59").xdata;
    hdxf_test::assert_eq(static_cast<int>(xdata.size()), 1, "ACAD_PSEXT");
    hdxf_test::assert_eq(xdata[0].app_name, "ACAD_PSEXT");
    hdxf_test::assert_eq(static_cast<int>(xdata[0].items.size()), 5);

    // BLKREFS and XDATA on BLOCK_RECORD
    const hdxf::BlockRecord& arch = block(dwg, "_ArchTick").record;
    hdxf_test::assert_eq(arch.handle, "4A4");
    hdxf_test::assert_eq(static_cast<int>(arch.block_refs.size()), 2);
    hdxf_test::assert_eq(arch.block_refs[1], "661");
    const hdxf::BlockRecord& my = block(dwg, "MyBlock").record;
    hdxf_test::assert_eq(static_cast<int>(my.xdata.size()), 2);
    hdxf_test::assert_eq(my.xdata[1].app_name, "ACAD");
    hdxf_test::assert_eq(my.xdata[1].items[0].value, "DesignCenter Data");

    // Every block has its record; none is left over in raw_tables()
    for (const auto& b : dwg.blocks()) {
        hdxf_test::assert_false(b.record.handle.empty(), "record of " + b.name);
    }
    for (const auto& t : dwg.raw_tables()) {
        if (t.name == "BLOCK_RECORD") hdxf_test::assert_true(t.entries.empty(), "left over");
    }
}

TEST(objects_layouts_and_block_records_2018) {
    hdxf::Drawing dwg;
    if (!load_external(dwg, "acadsharp/sample_AC1032_ascii.dxf")) return;
    hdxf_test::assert_eq(static_cast<int>(dwg.objects().size()), 317, "objects");
    check_sample_layouts(dwg);

    const hdxf::BlockRecord& model = block(dwg, "*Model_Space").record;
    hdxf_test::assert_eq(model.insert_units, 0, "70");
    hdxf_test::assert_eq(model.explodable, 1, "280");
    hdxf_test::assert_eq(model.scalable, 0, "281");
}

// The binary twin gives the same model. The two files are separate saves in AutoCAD,
// which gives a few objects new handles on each save (ACAD_2007HEADERVARS_RT is 14A2 in
// the ASCII file and 24B2 in the binary one), so handles are not compared.
TEST(objects_ascii_and_binary_2000_same) {
    hdxf::Drawing ascii;
    hdxf::Drawing binary;
    if (!load_external(ascii, "acadsharp/sample_AC1015_ascii.dxf")) return;
    if (!load_external(binary, "acadsharp/sample_AC1015_binary.dxf")) return;

    hdxf_test::assert_eq(static_cast<int>(binary.objects().size()),
                         static_cast<int>(ascii.objects().size()), "objects");
    for (size_t i = 0; i < ascii.objects().size(); ++i) {
        const hdxf::Object& a = ascii.objects()[i];
        const hdxf::Object& b = binary.objects()[i];
        std::string at = "object " + std::to_string(i) + " (" + a.handle + ")";
        hdxf_test::assert_true(a.type == b.type, at + " type");
        hdxf_test::assert_eq(static_cast<int>(b.reactors.size()),
                             static_cast<int>(a.reactors.size()), at + " reactors");
        hdxf_test::assert_eq(b.xdictionary.empty() ? 0 : 1, a.xdictionary.empty() ? 0 : 1,
                             at + " xdictionary");
        hdxf_test::assert_eq(static_cast<int>(b.xdata.size()),
                             static_cast<int>(a.xdata.size()), at + " xdata");
        if (a.type == hdxf::ObjectType::Dictionary) {
            const auto& ad = a.as_dictionary().entries;
            const auto& bd = b.as_dictionary().entries;
            hdxf_test::assert_eq(static_cast<int>(bd.size()), static_cast<int>(ad.size()));
            // Names below the root can be per save too (AcDsSchemas: serial numbers)
            for (size_t k = 0; i == 0 && k < ad.size(); ++k) {
                hdxf_test::assert_eq(bd[k].name, ad[k].name, at + " entry");
            }
        }
        if (a.type == hdxf::ObjectType::Unknown) {
            hdxf_test::assert_eq(b.as_unknown().object_type, a.as_unknown().object_type, at);
        }
        if (a.type == hdxf::ObjectType::XRecord) {
            hdxf_test::assert_eq(static_cast<int>(b.as_xrecord().data.size()),
                                 static_cast<int>(a.as_xrecord().data.size()), at + " data");
        }
    }
    check_sample_layouts(binary);
}

// R13/R14 XRECORD has no cloning flag (280): the data starts right after AcDbXrecord
TEST(objects_xrecord_r14_no_cloning_flag) {
    hdxf::Drawing dwg;
    if (!load_external(dwg, "ezdxf/small_r14.dxf")) return;

    const hdxf::XRecordData& d = object(dwg, "39").as_xrecord();
    hdxf_test::assert_false(d.data.empty(), "data");
    hdxf_test::assert_eq(d.data[0].code, 90);

    // 1000 without 1001 is data in an XRECORD, not XDATA
    const hdxf::Object& dstyle = object(dwg, "3D");
    hdxf_test::assert_true(dstyle.xdata.empty(), "no XDATA");
    hdxf_test::assert_eq(dstyle.as_xrecord().data[0].code, 1000);
    hdxf_test::assert_eq(dstyle.as_xrecord().data[0].value, "DSTYLE");
}

// R12 has no OBJECTS and no BLOCK_RECORD
TEST(objects_none_for_r12) {
    hdxf::Drawing dwg = r12_drawing_read_back();
    hdxf_test::assert_true(dwg.objects().empty(), "objects");
    hdxf_test::assert_true(dwg.layouts().empty(), "layouts");
    for (const auto& b : dwg.blocks()) {
        hdxf_test::assert_true(b.record.handle.empty() && b.record.raw.empty(), "record");
    }
}
