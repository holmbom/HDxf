#include "test_framework.hpp"
#include "test_drawing_builder.hpp"
#include "hdxf.hpp"
#include <filesystem>
#include <string>

// ---------------------------------------------------------------------------
// The common fields of DXF 2000 (phase 1, step 1.3 — doc/plans/2026-10-23-fas-1-dxf-2000.md):
// lineweight, plot style, layer plot flag, complex linetypes, the 2000 dimension
// variables, the UCS of VPORT/VIEW/UCS and the view of VIEWPORT.
//
// Expected values are read from ACadSharp's AutoCAD sample, not from HDxf.
// ---------------------------------------------------------------------------

namespace {

std::filesystem::path external(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(name);
}

bool load_sample(hdxf::Drawing& dwg) {
    std::filesystem::path path = external("acadsharp/sample_AC1015_ascii.dxf");
    if (!std::filesystem::exists(path)) {
        hdxf_test::skip("external test file missing");
        return false;
    }
    hdxf_test::assert_true(dwg.load(path), dwg.last_error());
    return true;
}

template <typename T>
const T& named(const std::vector<T>& list, const std::string& name) {
    for (const auto& x : list) {
        if (x.name == name) return x;
    }
    hdxf_test::assert_true(false, "\"" + name + "\" not found");
    return list.front();
}

const hdxf::Entity& entity(const hdxf::Drawing& dwg, const std::string& handle) {
    for (const auto& e : dwg.entities()) {
        if (e.props.handle == handle) return e;
    }
    for (const auto& b : dwg.blocks()) {
        for (const auto& e : b.entities) {
            if (e.props.handle == handle) return e;
        }
    }
    hdxf_test::assert_true(false, "entity " + handle + " not found");
    return dwg.entities().front();
}

hdxf::Drawing save_and_load(hdxf::Drawing& dwg, hdxf::DxfFormat format = hdxf::DxfFormat::Ascii) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), "save: " + dwg.last_error());
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    return back;
}

} // namespace

TEST(common_2000_lineweight_and_plot_style) {
    hdxf::Drawing dwg;
    if (!load_sample(dwg)) return;

    const hdxf::Layer& zero = named(dwg.layers(), "0");
    hdxf_test::assert_eq(zero.lineweight, -3);
    hdxf_test::assert_eq(zero.plot_style_handle, "F");
    hdxf_test::assert_true(zero.plot, "plot");
    hdxf_test::assert_eq(named(dwg.layers(), "Layer_lw_035").lineweight, 35);
    hdxf_test::assert_false(named(dwg.layers(), "Layer_NoPlot").plot, "290 0");

    const hdxf::Entity& line = entity(dwg, "2CF");
    hdxf_test::assert_eq(line.props.lineweight, 13);
    hdxf_test::assert_eq(line.props.linetype, "ACAD_ISO02W100");
    hdxf_test::assert_eq(entity(dwg, "298").props.lineweight, -2, "ByBlock");
    hdxf_test::assert_eq(entity(dwg, "31F").props.lineweight, -1, "not written: ByLayer");
}

TEST(common_2000_complex_linetype) {
    hdxf::Drawing dwg;
    if (!load_sample(dwg)) return;

    const hdxf::Linetype& gas = named(dwg.linetypes(), "GAS_LINE");
    hdxf_test::assert_eq(static_cast<int>(gas.pattern.size()), 3);
    hdxf_test::assert_eq(static_cast<int>(gas.elements.size()), 3);
    hdxf_test::assert_eq(gas.pattern[1], -0.2);
    const hdxf::LinetypeElement& text = gas.elements[1];
    // Text (2) and upright (8): 74 2 in the file, upright from the linetype's round-trip XRECORD
    // ACAD_ROUNDTRIP_2011_LINETYPE (plan phase 5, B60) — 74 10 in AutoCAD's 2013 file
    hdxf_test::assert_eq(text.type, 10, "text, upright");
    hdxf_test::assert_eq(text.style, "Standard", "340 = 11 → Standard");
    hdxf_test::assert_eq(text.scale, 0.1);
    hdxf_test::assert_eq(text.offset.x, -0.1);
    hdxf_test::assert_eq(text.offset.y, -0.05);
    hdxf_test::assert_eq(text.text, "GAS");
    hdxf_test::assert_eq(gas.elements[0].type, 0);

    hdxf_test::assert_true(named(dwg.linetypes(), "Continuous").elements.empty(), "simple");
}

// A 2000 DIMSTYLE has only what differs from AutoCAD's defaults
TEST(common_2000_dimstyle) {
    hdxf::Drawing dwg;
    if (!load_sample(dwg)) return;

    const hdxf::DimStyle& standard = named(dwg.dim_styles(), "Standard");
    hdxf_test::assert_eq(standard.dimasz, 0.18, "AutoCAD default, not written");
    hdxf_test::assert_eq(standard.dimtih, 1, "AutoCAD default");
    hdxf_test::assert_eq(standard.dimdec, 4, "AutoCAD default");
    hdxf_test::assert_eq(standard.dimtxsty, "Standard", "340");

    const hdxf::DimStyle& iso = named(dwg.dim_styles(), "ISO-25");
    hdxf_test::assert_eq(iso.dimasz, 2.5);
    hdxf_test::assert_eq(iso.dimtih, 0);
    hdxf_test::assert_eq(iso.dimzin, 8);
    hdxf_test::assert_eq(iso.dimdec, 2);
    hdxf_test::assert_eq(iso.dimtdec, 2);
    hdxf_test::assert_eq(iso.dimalttd, 3);
    hdxf_test::assert_eq(iso.dimdsep, ',');
    hdxf_test::assert_eq(iso.dimtolj, 0);
    hdxf_test::assert_eq(iso.dimtzin, 8);

    const hdxf::DimStyle& custom = named(dwg.dim_styles(), "custom_style_ltypes");
    hdxf_test::assert_eq(custom.dimblk, "_BoxBlank", "342 = C80 → its block");
    hdxf_test::assert_eq(custom.dimlwd, 15);
    hdxf_test::assert_eq(custom.dimclrd, 4);
}

TEST(common_2000_vport_and_viewport) {
    hdxf::Drawing dwg;
    if (!load_sample(dwg)) return;

    const hdxf::Vport& active = dwg.vports().front();
    hdxf_test::assert_true(active.ucs_per_viewport, "65 1");
    hdxf_test::assert_eq(active.ucs_x_axis.x, 1.0);

    const hdxf::ViewportData& vp = entity(dwg, "267").as_viewport();
    hdxf_test::assert_eq(vp.view_center.x, 7.981302901350615);
    hdxf_test::assert_eq(vp.view_center.y, 3.967191609810657);
    hdxf_test::assert_eq(vp.view_height, 8.747244094488189);
    hdxf_test::assert_eq(vp.snap_spacing.x, 0.5);
    hdxf_test::assert_eq(vp.grid_spacing.y, 0.5);
    hdxf_test::assert_eq(vp.view_direction.z, 1.0);
    hdxf_test::assert_eq(vp.circle_zoom, 1000);
    hdxf_test::assert_eq(vp.status_flags, 819232);
    hdxf_test::assert_eq(vp.ucs_per_viewport, 1);
}

// Everything set in memory is written as 2000 and read back the same
TEST(common_2000_write_and_read) {
    for (auto format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
        hdxf::Layer& hidden = dwg.add_layer("Hidden", hdxf::Color::blue());
        hidden.plot = false;
        hidden.lineweight = 50;
        dwg.add_layer("Frozen here");
        dwg.add_text_style("Notes", "romans.shx");

        hdxf::Linetype& lt = dwg.add_linetype("FENCE", "Fence ---X---", {1.0, -0.5, -0.5});
        lt.total_length = 2.0;
        lt.elements.resize(3);
        lt.elements[1].type = 2;
        lt.elements[1].style = "Notes";
        lt.elements[1].text = "X";
        lt.elements[1].scale = 0.2;
        lt.elements[1].offset = {-0.1, -0.1};

        hdxf::DimStyle& ds = dwg.add_dim_style("Metric");
        ds.dimtxsty = "Notes";
        ds.dimdec = 2;
        ds.dimdsep = ',';
        ds.dimlwd = 25;

        dwg.add_line({0, 0, 0}, {10, 0, 0}, "Hidden").props.lineweight = 35;

        hdxf::Entity& v = dwg.add_viewport({100, 100, 0}, 200, 150);
        v.props.paper_space = 1;
        hdxf::ViewportData& vd = v.as_viewport();
        vd.view_center = {5, 5};
        vd.view_height = 20;
        vd.twist_angle = 15;
        vd.frozen_layers = {"Frozen here"};

        hdxf::Drawing back = save_and_load(dwg, format);
        const hdxf::Layer& h = named(back.layers(), "Hidden");
        hdxf_test::assert_false(h.plot, "plot");
        hdxf_test::assert_eq(h.lineweight, 50);
        hdxf_test::assert_false(h.plot_style_handle.empty(), "default plot style");

        const hdxf::Linetype& fence = named(back.linetypes(), "FENCE");
        hdxf_test::assert_eq(static_cast<int>(fence.elements.size()), 3);
        hdxf_test::assert_eq(fence.elements[1].style, "Notes");
        hdxf_test::assert_eq(fence.elements[1].text, "X");
        hdxf_test::assert_eq(fence.elements[1].scale, 0.2);

        const hdxf::DimStyle& m = named(back.dim_styles(), "Metric");
        hdxf_test::assert_eq(m.dimtxsty, "Notes");
        hdxf_test::assert_eq(m.dimdec, 2);
        hdxf_test::assert_eq(m.dimdsep, ',');
        hdxf_test::assert_eq(m.dimlwd, 25);
        hdxf_test::assert_eq(m.dimasz, 2.5, "all values are written");

        hdxf_test::assert_eq(back.entities()[0].props.lineweight, 35);
        const hdxf::ViewportData& bv = back.entities()[1].as_viewport();
        hdxf_test::assert_eq(bv.view_center.x, 5.0);
        hdxf_test::assert_eq(bv.view_height, 20.0);
        hdxf_test::assert_eq(bv.twist_angle, 15.0);
        hdxf_test::assert_eq(static_cast<int>(bv.frozen_layers.size()), 1);
        hdxf_test::assert_eq(bv.frozen_layers[0], "Frozen here");
    }
}

// A style or block that does not exist cannot be pointed to
TEST(common_2000_missing_names_are_errors) {
    hdxf::Drawing a(hdxf::DxfVersion::R2000);
    a.add_dim_style("D").dimtxsty = "NoSuchStyle";
    hdxf_test::assert_true(a.save_to_memory().empty(), "saved");
    hdxf_test::assert_true(a.last_error().find("NoSuchStyle") != std::string::npos, a.last_error());

    hdxf::Drawing b(hdxf::DxfVersion::R2000);
    b.add_dim_style("D").dimldrblk = "NoSuchBlock";
    hdxf_test::assert_true(b.save_to_memory().empty(), "saved");
    hdxf_test::assert_true(b.last_error().find("NoSuchBlock") != std::string::npos, b.last_error());
}

// An R12 VIEWPORT keeps its view in ACAD/MVIEW XDATA; as 2000 it becomes the fields
TEST(common_2000_viewport_from_r12_mview) {
    hdxf::Drawing dwg = r12_drawing_read_back();
    hdxf::Entity* vp = nullptr;
    for (auto& e : dwg.entities()) {
        if (e.type == hdxf::EntityType::Viewport) vp = &e;
    }
    hdxf_test::assert_true(vp != nullptr, "viewport in the R12 drawing");
    // Change the R12 view, so the test sees that it is the XDATA that is used
    for (auto& xd : vp->props.xdata) {
        if (xd.app_name != "ACAD") continue;
        int reals = 0;
        for (auto& item : xd.items) {
            if (item.code != 1040) continue;
            ++reals;
            if (reals == 1) item.value = "30.0";     // twist
            if (reals == 2) item.value = "75.0";     // view height
            if (reals == 3) item.value = "12.5";     // view center x
        }
    }

    dwg.set_version(hdxf::DxfVersion::R2000);
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    hdxf_test::assert_true(std::string(out.begin(), out.end()).find("MVIEW") == std::string::npos,
                           "MVIEW is R12 only");
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    const hdxf::ViewportData* bv = nullptr;
    for (const auto& e : back.entities()) {
        if (e.type == hdxf::EntityType::Viewport) bv = &e.as_viewport();
    }
    hdxf_test::assert_true(bv != nullptr, "viewport");
    hdxf_test::assert_eq(bv->twist_angle, 30.0);
    hdxf_test::assert_eq(bv->view_height, 75.0);
    hdxf_test::assert_eq(bv->view_center.x, 12.5);
    hdxf_test::assert_eq(bv->circle_zoom, 100, "from MVIEW");
    hdxf_test::assert_true((bv->status_flags & 32768) != 0, "always set");
}

// R12 has none of these fields: its output is unchanged
TEST(common_2000_not_in_r12) {
    hdxf::Drawing dwg;
    dwg.add_layer("L").lineweight = 50;
    dwg.add_line({0, 0, 0}, {1, 0, 0}, "L").props.lineweight = 35;
    std::vector<uint8_t> out = dwg.save_to_memory();
    std::string s = hdxf_test::lf_text(out);
    hdxf_test::assert_true(s.find("\n370\n") == std::string::npos, "370 in R12");
}

// The UCS references (345/346), the orthographic origins of a UCS and the clip boundary
// of a viewport, written and read back; UCS records are named in the API
TEST(common_2000_ucs_references) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    hdxf::Ucs base;
    base.name = "Base";
    dwg.ucs_list().push_back(base);
    hdxf::Ucs front;
    front.name = "Front";
    front.origin = {1, 2, 3};
    front.base_ucs = "Base";
    front.orthographic = {{1, {10, 0, 0}}, {3, {0, 20, 0}}};
    dwg.ucs_list().push_back(front);

    hdxf::Vport vp;
    vp.ucs = "Front";
    dwg.vports().push_back(vp);
    hdxf::View view;
    view.name = "V";
    view.has_ucs = true;
    view.ucs = "Front";
    view.base_ucs = "Base";
    dwg.views().push_back(view);

    dwg.add_circle({0, 0, 0}, 5);
    hdxf::Entity& v = dwg.add_viewport({10, 10, 0}, 20, 20);
    v.props.paper_space = 1;
    v.as_viewport().id = 2;
    v.as_viewport().ucs = "Front";
    v.as_viewport().base_ucs = "Base";

    // The clip boundary is an entity: give the circle its handle first
    std::vector<uint8_t> first = dwg.save_to_memory();
    hdxf_test::assert_false(first.empty(), dwg.last_error());
    dwg.entities()[1].as_viewport().clip_boundary_handle = dwg.entities()[0].props.handle;

    hdxf::Drawing back = save_and_load(dwg);
    const hdxf::Ucs& f = named(back.ucs_list(), "Front");
    hdxf_test::assert_eq(f.base_ucs, "Base");
    hdxf_test::assert_eq(static_cast<int>(f.orthographic.size()), 2);
    hdxf_test::assert_eq(f.orthographic[1].type, 3);
    hdxf_test::assert_eq(f.orthographic[1].origin.y, 20.0);
    hdxf_test::assert_eq(back.vports()[0].ucs, "Front");
    hdxf_test::assert_eq(named(back.views(), "V").ucs, "Front");
    hdxf_test::assert_eq(named(back.views(), "V").base_ucs, "Base");
    const hdxf::ViewportData& bv = back.entities()[1].as_viewport();
    hdxf_test::assert_eq(bv.ucs, "Front");
    hdxf_test::assert_eq(bv.base_ucs, "Base");
    hdxf_test::assert_eq(bv.clip_boundary_handle, back.entities()[0].props.handle);

    hdxf::Drawing missing(hdxf::DxfVersion::R2000);
    hdxf::Vport bad;
    bad.ucs = "NoSuchUcs";
    missing.vports().push_back(bad);
    hdxf_test::assert_true(missing.save_to_memory().empty(), "saved");
    hdxf_test::assert_true(missing.last_error().find("NoSuchUcs") != std::string::npos,
                           missing.last_error());
}

// The DIMSTYLE table's list of styles that have been current: $DIMSTYLE
TEST(common_2000_dimstyle_table_recent_list) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    hdxf::DimStyle& iso = dwg.add_dim_style("ISO");
    (void)iso;
    dwg.set_header_string("$DIMSTYLE", 2, "ISO");
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    std::string s = hdxf_test::lf_text(out);
    std::string handle = named(dwg.dim_styles(), "ISO").handle;
    hdxf_test::assert_true(s.find("100\nAcDbDimStyleTable\n 71\n     1\n340\n" + handle + "\n") !=
                           std::string::npos, "71 1 340 " + handle);
}
