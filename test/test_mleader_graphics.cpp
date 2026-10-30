#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "dxf_structure.hpp"
#include "extents.hpp"
#include "mleader_graphics.hpp"
#include "text_layout.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// MULTILEADER graphics (phase 3, step 3.4 — plan B38): the proxy graphics AutoCAD writes for a
// multileader (ACadSharp's samples in 2000, 2004 and 2007, the Oxcart drawings' balloons and
// notes), its extents, and add_mleader() placing the text as AutoCAD does.
// ---------------------------------------------------------------------------

namespace {

using Record = std::vector<std::pair<int, std::string>>;

std::vector<Record> records_of(const std::vector<uint8_t>& data) {
    std::string s(data.begin(), data.end());
    std::istringstream in(s, std::ios::binary);
    hdxf::internal::GroupReader reader(in);
    reader.detect_format();
    std::vector<Record> out;
    hdxf::internal::GroupCode gc;
    while (reader.read(gc)) {
        if (gc.code == 999) continue;
        if (gc.code == 0 || out.empty()) out.emplace_back();
        out.back().push_back({gc.code, dxf_structure::number_text(gc.code, gc.value)});
    }
    return out;
}

std::vector<uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::filesystem::path external(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(name);
}

bool has_files(const std::vector<std::string>& names) {
    for (const auto& n : names) {
        if (!std::filesystem::exists(external(n))) {
            hdxf_test::skip("external test file missing: " + n);
            return false;
        }
    }
    return true;
}

std::filesystem::path windows_font(const std::string& file) {
    const char* windir = std::getenv("WINDIR");
    return windir ? hdxf::path_from_utf8(windir) / "Fonts" / file : std::filesystem::path(file);
}

// The fonts of Windows (Arial) for the test, the paths as they were after
struct FontPaths {
    std::vector<std::filesystem::path> saved = hdxf::font_paths();
    FontPaths() { hdxf::set_font_paths({windows_font("arial.ttf").parent_path()}); }
    ~FontPaths() { hdxf::set_font_paths(saved); }
};

bool have_arial() {
    if (std::filesystem::exists(windows_font("arial.ttf"))) return true;
    hdxf_test::skip("arial.ttf missing");
    return false;
}

hdxf::Drawing load(const std::vector<uint8_t>& data) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(data.data(), data.size()), dwg.last_error());
    return dwg;
}

Record record(const std::vector<Record>& records, const std::string& handle) {
    for (const auto& r : records) {
        for (const auto& [code, value] : r) {
            if ((code == 5 || code == 105) && value == handle) return r;
            if (code == 100) break;
        }
    }
    return {};
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

// The proxy graphics of a record: the 310 groups after its 92 (or 160) in AcDbEntity
std::vector<uint8_t> proxy_of(const Record& r) {
    std::vector<uint8_t> out;
    bool in = false;
    for (const auto& [code, v] : r) {
        if (!in && (code == 92 || code == 160)) { in = true; continue; }
        if (!in) continue;
        if (code != 310) break;
        for (size_t i = 0; i + 1 < v.size(); i += 2) {
            out.push_back(static_cast<uint8_t>(hex_digit(v[i]) * 16 + hex_digit(v[i + 1])));
        }
    }
    return out;
}

// A proxy graphics command: its number, its numbers (integers and reals) and its strings
struct Command {
    int type = 0;
    std::vector<double> numbers;
    std::vector<std::string> strings;
};

int32_t i32(const std::vector<uint8_t>& b, size_t o) {
    uint32_t u = 0;
    for (int k = 0; k < 4; ++k) u |= static_cast<uint32_t>(o + k < b.size() ? b[o + k] : 0) << (8 * k);
    return static_cast<int32_t>(u);
}

double f64(const std::vector<uint8_t>& b, size_t o) {
    uint64_t u = 0;
    for (int k = 0; k < 8; ++k) u |= static_cast<uint64_t>(o + k < b.size() ? b[o + k] : 0) << (8 * k);
    double d;
    std::memcpy(&d, &u, 8);
    return d;
}

// A 0-terminated string padded to 4 bytes (wide: UTF-16, kept as its bytes)
std::string str(const std::vector<uint8_t>& b, size_t& o, bool wide) {
    size_t start = o;
    std::string s;
    if (wide) {
        while (o + 1 < b.size() && (b[o] || b[o + 1])) { s += static_cast<char>(b[o]); s += static_cast<char>(b[o + 1]); o += 2; }
        o += 2;
    } else {
        while (o < b.size() && b[o]) s += static_cast<char>(b[o++]);
        o += 1;
    }
    while ((o - start) % 4) ++o;
    return s;
}

std::vector<Command> decode(const std::vector<uint8_t>& b) {
    std::vector<Command> out;
    if (b.size() < 8) return out;
    int count = i32(b, 4);
    size_t o = 8;
    for (int n = 0; n < count && o + 8 <= b.size(); ++n) {
        int size = i32(b, o);
        Command c;
        c.type = i32(b, o + 4);
        size_t p = o + 8, end = o + static_cast<size_t>(size);
        auto reals = [&](int k) { for (int i = 0; i < k; ++i, p += 8) c.numbers.push_back(f64(b, p)); };
        auto ints = [&](int k) { for (int i = 0; i < k; ++i, p += 4) c.numbers.push_back(i32(b, p)); };
        switch (c.type) {
            case 6: case 7: { int k = i32(b, p); ints(1); reals(3 * k); break; }
            case 2: reals(7); break;
            case 4: reals(13); ints(1); break;
            case 29: reals(16); break;
            case 32: { int k = i32(b, p); ints(1); reals(3 * k + 3); break; }
            case 44: reals(11); ints(1); break;
            case 11: case 38: {
                bool wide = c.type == 38;
                reals(9);
                c.strings.push_back(str(b, p, wide));
                ints(2);
                reals(4);
                ints(5);
                if (wide) ints(4);
                c.strings.push_back(str(b, p, wide));
                c.strings.push_back(str(b, p, wide));
                if (wide) c.strings.push_back(str(b, p, wide));
                break;
            }
            default:
                while (p + 4 <= end) ints(1);
                break;
        }
        out.push_back(c);
        o = end;
    }
    return out;
}

// Two proxy graphics alike: the same commands, numbers to `tolerance` (relative to the
// coordinates' size); the layer index not compared when `layers` is false (AutoCAD counts
// erased layers the DXF file does not have)
bool same_graphics(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, double tolerance, bool layers,
                   std::string& why) {
    std::vector<Command> x = decode(a), y = decode(b);
    if (x.size() != y.size()) {
        why = std::to_string(x.size()) + " / " + std::to_string(y.size()) + " commands";
        return false;
    }
    double scale = 1.0;
    for (const auto& c : x) {
        if (c.type == 6 || c.type == 7 || c.type == 2 || c.type == 4 || c.type == 11 || c.type == 38) {
            for (double v : c.numbers) scale = std::max(scale, std::fabs(v));
        }
    }
    for (size_t i = 0; i < x.size(); ++i) {
        const Command& p = x[i];
        const Command& q = y[i];
        why = "command " + std::to_string(i) + " (" + std::to_string(p.type) + ")";
        if (p.type != q.type || p.numbers.size() != q.numbers.size() || p.strings != q.strings) return false;
        if (p.type == 16 && !layers) continue;
        for (size_t k = 0; k < p.numbers.size(); ++k) {
            if (std::fabs(p.numbers[k] - q.numbers[k]) > tolerance * scale) {
                why += " value " + std::to_string(k);
                return false;
            }
        }
    }
    why.clear();
    return true;
}

hdxf::internal::ProxyTables tables_of(const hdxf::Drawing& dwg) {
    hdxf::internal::ProxyTables t;
    for (const auto& l : dwg.layers()) t.layers.push_back(l.name);
    for (const auto& l : dwg.linetypes()) t.linetypes.push_back(l.name);
    return t;
}

// ACadSharp's multileaders whose text flows top to bottom (172 3) in Arial: AutoCAD draws
// their lines in a row with the vertical flag, by a rule its files do not show (a checkpoint)
bool vertical_text(const std::string& handle) {
    return handle == "B22" || handle == "B23" || handle == "B24";
}

} // namespace

// Every multileader of ACadSharp's 2000, 2004, 2007 and 2010 samples written anew has AutoCAD's
// proxy graphics: the same commands, the numbers to the last digits — in 2010 B27 attached
// vertically too (no landing, B47)
TEST(mleader_proxy_graphics_as_autocad) {
    if (!have_arial()) return;
    FontPaths fonts;
    for (const char* name : {"acadsharp/sample_AC1015_ascii.dxf", "acadsharp/sample_AC1018_ascii.dxf",
                             "acadsharp/sample_AC1021_ascii.dxf", "acadsharp/sample_AC1021_binary.dxf",
                             "acadsharp/sample_AC1024_ascii.dxf", "acadsharp/sample_AC1024_binary.dxf"}) {
        if (!has_files({name})) continue;
        std::vector<uint8_t> in = read_bytes(external(name));
        std::vector<Record> before = records_of(in);
        hdxf::Drawing dwg = load(in);
        std::vector<std::string> handles;
        for (auto& e : dwg.entities()) {
            if (e.type != hdxf::EntityType::MLeader || vertical_text(e.props.handle)) continue;
            e.raw = hdxf::RawRecord();
            handles.push_back(e.props.handle);
        }
        hdxf_test::assert_eq(static_cast<int>(handles.size()), 12, name);
        bool binary = std::string(name).find("binary") != std::string::npos;
        std::vector<uint8_t> out = dwg.save_to_memory(binary ? hdxf::DxfFormat::Binary : hdxf::DxfFormat::Ascii);
        hdxf_test::assert_false(out.empty(), dwg.last_error());
        std::vector<Record> after = records_of(out);
        for (const auto& h : handles) {
            std::vector<uint8_t> theirs = proxy_of(record(before, h)), ours = proxy_of(record(after, h));
            hdxf_test::assert_false(theirs.empty(), "AutoCAD's proxy graphics");
            hdxf_test::assert_eq(static_cast<int>(ours.size()), static_cast<int>(theirs.size()),
                                 std::string(name) + " " + h + ": size");
            std::string why;
            hdxf_test::assert_true(same_graphics(theirs, ours, 1e-12, true, why), std::string(name) + " " + h + ": " + why);
        }
    }
}

// Proxy graphics without the fill command (20) right after an arrowhead's polygon (7): the
// repeated "fill on" AutoCAD's newer builds write and its older ones do not
std::vector<uint8_t> without_repeated_fill(const std::vector<uint8_t>& b) {
    if (b.size() < 8) return b;
    std::vector<uint8_t> out(b.begin(), b.begin() + 8);
    int count = i32(b, 4), kept = 0, previous = 0;
    size_t o = 8;
    for (int n = 0; n < count && o + 8 <= b.size(); ++n) {
        size_t size = static_cast<size_t>(i32(b, o));
        int type = i32(b, o + 4);
        if (!(type == 20 && previous == 7)) {
            out.insert(out.end(), b.begin() + static_cast<long>(o), b.begin() + static_cast<long>(o + size));
            ++kept;
        }
        previous = type;
        o += size;
    }
    auto put = [&](size_t at, uint32_t v) { for (int k = 0; k < 4; ++k) out[at + k] = static_cast<uint8_t>(v >> (8 * k)); };
    put(0, static_cast<uint32_t>(out.size()));
    put(4, static_cast<uint32_t>(kept));
    return out;
}

// LibreDWG's 640 (AutoCAD's file; the only multileader of AutoCAD's with 295 1, text_extended —
// B58's checkpoint) written anew in 2007, 2010 and 2013: AutoCAD's proxy graphics command for
// command, the text in Standard's txt.shx too. The flag changes nothing in the layout. The one
// difference: AutoCAD repeats "fill on" (20 1) after the arrowhead in ACadSharp's and Oxcart's files
// (and HDxf with them), not in LibreDWG's — by the AutoCAD that saved the file, not by the flag:
// 24 multileaders with 295 0 in the collections' files saved 2015 … 2020 lack it too; the 275
// others, nearly all in files saved 2023 … 2026, have it (plan phase 5, step 5.3)
TEST(mleader_text_extended_proxy_graphics) {
    const std::filesystem::path autocad_fonts = "C:/Program Files/Autodesk/DWG TrueView 2027 - English/Fonts";
    if (!std::filesystem::exists(autocad_fonts / "txt.shx")) {
        hdxf_test::skip("AutoCAD's txt.shx missing");
        return;
    }
    FontPaths fonts;
    hdxf::set_font_paths({autocad_fonts});
    int compared = 0;
    for (const char* name : {"libre/example_2007.dxf", "libre/example_2010.dxf", "libre/example_2013.dxf"}) {
        if (!has_files({name})) continue;
        std::vector<uint8_t> in = read_bytes(external(name));
        std::vector<Record> before = records_of(in);
        hdxf::Drawing dwg = load(in);
        bool found = false;
        for (auto& e : dwg.entities()) {
            if (e.props.handle != "640") continue;
            found = true;
            hdxf_test::assert_true(e.as_mleader().text_extended, std::string(name) + ": 640's text_extended");
            e.raw = hdxf::RawRecord();
        }
        hdxf_test::assert_true(found, std::string(name) + ": 640");
        std::vector<uint8_t> out = dwg.save_to_memory(hdxf::DxfFormat::Ascii);
        hdxf_test::assert_false(out.empty(), dwg.last_error());
        std::vector<uint8_t> theirs = proxy_of(record(before, "640"));
        std::vector<uint8_t> ours = proxy_of(record(records_of(out), "640"));
        hdxf_test::assert_false(theirs.empty(), "AutoCAD's proxy graphics");
        hdxf_test::assert_eq(static_cast<int>(decode(ours).size()), static_cast<int>(decode(theirs).size()) + 1,
                             std::string(name) + ": AutoCAD's commands and the repeated fill");
        std::string why;
        bool same = same_graphics(theirs, without_repeated_fill(ours), 1e-12, true, why);
        hdxf_test::assert_true(same, std::string(name) + " 640: " + why);
        ++compared;
    }
    if (compared == 0) hdxf_test::skip("LibreDWG's files missing");
}

// The Oxcart drawings (Autodesk's 2013 files, AC1027): balloons with an attribute and with a block of
// MTEXT and arcs (the leader stops at the arcs), notes with underlines, a trailing space, an
// empty text. Their DXF has 6 decimals: the graphics agree to that.
TEST(mleader_proxy_graphics_oxcart) {
    if (!have_arial()) return;
    FontPaths fonts;
    int compared = 0;
    for (const char* name : {"oxcart/cryo_clamp_upper.dxf", "oxcart/CY7_diode_clamp.dxf", "oxcart/cryo_interface_v3.dxf",
                             "oxcart/cryo_interface_v2_weld.dxf"}) {
        if (!has_files({name})) continue;
        std::vector<uint8_t> in = read_bytes(external(name));
        std::vector<Record> recs = records_of(in);
        hdxf::Drawing dwg = load(in);
        hdxf::internal::ProxyTables tables = tables_of(dwg);
        hdxf::internal::TextLayout layout(dwg);
        for (const auto& e : dwg.entities()) {
            if (e.type != hdxf::EntityType::MLeader) continue;
            std::vector<uint8_t> theirs = proxy_of(record(recs, e.props.handle));
            std::vector<uint8_t> ours = hdxf::internal::mleader_graphics(dwg, e, tables, layout, true);
            std::string why;
            hdxf_test::assert_true(same_graphics(theirs, ours, 2e-8, false, why),
                                   std::string(name) + " " + e.props.handle + ": " + why);
            ++compared;
        }
    }
    if (compared) hdxf_test::assert_eq(compared, 8, "multileaders compared");
}

// AutoCAD's own text placement: the base point (10) and the text location (12) made again from
// the leader, the text and the context's attachments, in ACadSharp's samples and Oxcart's notes
TEST(mleader_text_placed_as_autocad) {
    if (!have_arial()) return;
    FontPaths fonts;
    int placed = 0;
    for (const char* name : {"acadsharp/sample_AC1015_ascii.dxf", "acadsharp/sample_AC1021_ascii.dxf",
                             "acadsharp/sample_AC1024_ascii.dxf", "oxcart/cryo_interface_v3.dxf",
                             "oxcart/cryo_interface_v2_weld.dxf"}) {
        if (!has_files({name})) continue;
        hdxf::Drawing dwg = load(read_bytes(external(name)));
        for (const auto& e : dwg.entities()) {
            if (e.type != hdxf::EntityType::MLeader || vertical_text(e.props.handle)) continue;
            const hdxf::MLeaderData& d = e.as_mleader();
            if (!d.context.has_text) continue;
            // B27 as 2000-2007 have it: AutoCAD made its vertical attachment (2010) horizontal for
            // them, its landing without length. In 2010 it is attached vertically (B47)
            if (e.props.handle == "B27" && dwg.version() < hdxf::DxfVersion::R2010) continue;
            hdxf::MLeaderData copy = d;
            copy.context.base_point = {};
            copy.context.text_location = {};
            hdxf::internal::place_mleader_text(dwg, copy);
            auto near = [](const hdxf::Point3& p, const hdxf::Point3& q) {
                double tol = 2e-6 * std::max(1.0, std::max(std::fabs(q.x), std::fabs(q.y)));
                return std::fabs(p.x - q.x) <= tol && std::fabs(p.y - q.y) <= tol;
            };
            hdxf_test::assert_true(near(copy.context.base_point, d.context.base_point),
                                   std::string(name) + " " + e.props.handle + ": base point");
            hdxf_test::assert_true(near(copy.context.text_location, d.context.text_location),
                                   std::string(name) + " " + e.props.handle + ": text location");
            ++placed;
        }
    }
    hdxf_test::assert_true(placed > 0 || !std::filesystem::exists(external("acadsharp/sample_AC1021_ascii.dxf")),
                           "multileaders placed");
}

// A new text multileader: the style's values, the landing to the leader's side, the text
// right-justified for a leader from the right, AutoCAD's override flags; written with its proxy
// graphics in 2000, 2004 and 2007 ($PROXYGRAPHICS 0: without)
TEST(mleader_add_text) {
    if (!have_arial()) return;
    FontPaths fonts;
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004, hdxf::DxfVersion::R2007}) {
        hdxf::Drawing dwg(v);
        // Standard in Arial, so the text is laid out
        hdxf::TextStyle& standard = dwg.add_text_style("Standard", "arial.ttf");
        standard.font_file = "arial.ttf";
        standard.font_family = "Arial";
        hdxf::Entity& left = dwg.add_mleader({{0, 0, 0}, {3, 2, 0}}, "Left");
        const hdxf::MLeaderData& l = left.as_mleader();
        hdxf_test::assert_eq(l.override_flags, 279552, "override flags");
        hdxf_test::assert_true(!l.style_handle.empty(), "the Standard style made");
        hdxf_test::assert_eq(l.context.text_attachment, 1, "top left");
        hdxf_test::assert_true(l.context.leaders[0].dogleg_direction.x == 1.0, "landing to the right");
        // The base point at the landing's end (middle of the top line: attachment 1)
        hdxf_test::assert_true(std::fabs(l.context.base_point.x - 3.36) < 1e-12 && std::fabs(l.context.base_point.y - 2.0) < 1e-12,
                               "base point at the landing's end");
        hdxf_test::assert_true(std::fabs(l.context.text_location.x - 3.45) < 1e-12, "text after the gap");
        hdxf::Entity& right = dwg.add_mleader({{10, 0, 0}, {7, 2, 0}}, "Right");
        const hdxf::MLeaderData& r = right.as_mleader();
        hdxf_test::assert_eq(r.context.text_attachment, 3, "top right");
        hdxf_test::assert_eq(r.context.text_align, 2, "aligned right");
        hdxf_test::assert_true(std::fabs(r.context.text_location.x - (7.0 - 0.36 - 0.09)) < 1e-12, "right edge before the gap");

        std::vector<uint8_t> out = dwg.save_to_memory();
        hdxf_test::assert_false(out.empty(), dwg.last_error());
        dxf_structure::check_structure(out, "multileaders");
        std::vector<Record> recs = records_of(out);
        Record first;
        for (const auto& rec : recs) {
            if (rec[0].second == "MULTILEADER") { first = rec; break; }
        }
        std::string left_handle;
        for (const auto& [code, val] : first) if (code == 5) left_handle = val;
        std::vector<uint8_t> graphics = proxy_of(first);
        std::vector<Command> commands = decode(graphics);
        hdxf_test::assert_true(commands.size() > 10, "proxy graphics written");
        bool text = false;
        for (const auto& c : commands) text = text || c.type == (v >= hdxf::DxfVersion::R2007 ? 38 : 11);
        hdxf_test::assert_true(text, "its text, Unicode from 2007");
        hdxf::Drawing back = load(out);
        int count = 0;
        for (const auto& e : back.entities()) count += e.type == hdxf::EntityType::MLeader;
        hdxf_test::assert_eq(count, 2, "read back");

        dwg.set_header_int("$PROXYGRAPHICS", 70, 0);
        std::vector<Record> none = records_of(dwg.save_to_memory());
        hdxf_test::assert_true(proxy_of(record(none, left_handle)).empty(), "no proxy graphics with $PROXYGRAPHICS 0");
    }
}

// A block multileader: the block at the landing's end, its attribute's default value; its graphics
// draw the block in its transform and the attribute after it; its extents hold the circle
TEST(mleader_add_block) {
    if (!have_arial()) return;
    FontPaths fonts;
    hdxf::Drawing dwg(hdxf::DxfVersion::R2007);
    hdxf::TextStyle& st = dwg.add_text_style("ArialStyle", "arial.ttf");
    st.font_family = "Arial";
    hdxf::Block& b = dwg.add_block("BALLOON");
    hdxf::Entity circle;
    circle.type = hdxf::EntityType::Circle;
    hdxf::CircleData cd;
    cd.radius = 0.5;
    circle.data = cd;
    b.entities.push_back(circle);
    hdxf::Entity attdef;
    attdef.type = hdxf::EntityType::Attdef;
    hdxf::AttdefData ad;
    ad.tag = "NR";
    ad.default_value = "7";
    ad.height = 0.36;
    ad.h_justify = 1;
    ad.v_justify = 2;
    ad.style = "ArialStyle";
    attdef.data = ad;
    attdef.props.handle = "ABC";
    b.entities.push_back(attdef);
    hdxf::Entity& e = dwg.add_mleader_block({{0, 0, 0}, {4, 3, 0}}, "BALLOON");
    const hdxf::MLeaderData& d = e.as_mleader();
    hdxf_test::assert_true(d.context.has_block && !d.context.has_text, "block content");
    hdxf_test::assert_true(std::fabs(d.context.block_position.x - 4.36) < 1e-12, "at the landing's end");
    hdxf_test::assert_eq(static_cast<int>(d.attributes.size()), 1, "attribute");
    hdxf_test::assert_eq(d.attributes[0].text, std::string("7"), "default value");
    hdxf::internal::TextLayout layout(dwg);
    std::vector<Command> c = decode(hdxf::internal::mleader_graphics(dwg, e, tables_of(dwg), layout, true));
    bool push = false, circle_drawn = false, attribute = false;
    for (size_t i = 0; i < c.size(); ++i) {
        push = push || c[i].type == 29;
        circle_drawn = circle_drawn || c[i].type == 2;
        if (c[i].type == 19 && c[i].numbers[0] == 15005 && i + 1 < c.size()) attribute = c[i + 1].type == 38;
    }
    hdxf_test::assert_true(push && circle_drawn && attribute, "block, circle and attribute drawn");
    hdxf::internal::Extents ext;
    hdxf::internal::entity_extents(dwg, e, ext);
    hdxf_test::assert_true(std::fabs(ext.max.x - 4.86) < 1e-12 && std::fabs(ext.max.y - 3.5) < 1e-12, "the circle in the extents");
    hdxf_test::assert_true(ext.min.x == 0.0 && ext.min.y == 0.0, "the arrowhead's tip in the extents");
}

// A block built in memory: its ATTDEF has no handle until it is saved — the multileader's link to
// it (330) is made with the handle it gets, so the first save writes the link and draws the value
TEST(mleader_block_attribute_link_in_memory) {
    if (!have_arial()) return;
    FontPaths fonts;
    hdxf::Drawing dwg(hdxf::DxfVersion::R2007);
    hdxf::TextStyle& st = dwg.add_text_style("ArialStyle", "arial.ttf");
    st.font_family = "Arial";
    hdxf::Block& b = dwg.add_block("BALLOON");
    hdxf::Entity circle;
    circle.type = hdxf::EntityType::Circle;
    hdxf::CircleData cd;
    cd.radius = 0.5;
    circle.data = cd;
    b.entities.push_back(circle);
    hdxf::Entity attdef;
    attdef.type = hdxf::EntityType::Attdef;
    hdxf::AttdefData ad;
    ad.tag = "NR";
    ad.default_value = "7";
    ad.height = 0.36;
    ad.style = "ArialStyle";
    attdef.data = ad;
    b.entities.push_back(attdef);
    dwg.add_mleader_block({{0, 0, 0}, {4, 3, 0}}, "BALLOON");
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    hdxf::Drawing back = load(out);
    std::string attdef_handle;
    for (const auto& e : back.find_block("BALLOON")->entities) {
        if (e.type == hdxf::EntityType::Attdef) attdef_handle = e.props.handle;
    }
    const hdxf::Entity* ml = nullptr;
    for (const auto& e : back.entities()) if (e.type == hdxf::EntityType::MLeader) ml = &e;
    hdxf_test::assert_true(ml != nullptr, "multileader read back");
    hdxf_test::assert_eq(static_cast<int>(ml->as_mleader().attributes.size()), 1, "attribute");
    hdxf_test::assert_false(attdef_handle.empty(), "ATTDEF handle");
    hdxf_test::assert_eq(ml->as_mleader().attributes[0].attdef_handle, attdef_handle, "330 links the ATTDEF");
    std::vector<Command> c = decode(proxy_of(record(records_of(out), ml->props.handle)));
    bool value = false;
    for (size_t i = 0; i + 1 < c.size(); ++i) {
        if (c[i].type == 19 && c[i].numbers[0] == 15005) value = c[i + 1].type == 38 && !c[i + 1].strings.empty() &&
                                                                 c[i + 1].strings[0] == std::string("7\0", 2);
    }
    hdxf_test::assert_true(value, "the attribute's value in the proxy graphics");
}

// A read multileader keeps its proxy graphics while what it draws is as it was; a change in what
// it draws with — its block's geometry, an entity removed from the block, its text style's font —
// gives new graphics, though the multileader itself did not change
TEST(mleader_proxy_follows_dependencies) {
    if (!have_arial()) return;
    FontPaths fonts;
    hdxf::Drawing dwg(hdxf::DxfVersion::R2007);
    hdxf::TextStyle& st = dwg.add_text_style("Notes", "arial.ttf");
    st.font_family = "Arial";
    hdxf::Block& b = dwg.add_block("BALLONG");
    hdxf::Entity circle;
    circle.type = hdxf::EntityType::Circle;
    hdxf::CircleData cd;
    cd.radius = 0.5;
    circle.data = cd;
    b.entities.push_back(circle);
    hdxf::Entity line;
    line.type = hdxf::EntityType::Line;
    line.data = hdxf::LineData{{-0.5, 0, 0}, {0.5, 0, 0}};
    b.entities.push_back(line);
    hdxf::Object& style = dwg.add_mleader_style("Notes");
    style.as_mleader_style().text_style = "Notes";
    dwg.add_mleader_block({{0, 0, 0}, {4, 3, 0}}, "BALLONG");
    hdxf::Entity& text = dwg.add_mleader({{10, 0, 0}, {14, 3, 0}}, "Text", "Notes");
    text.as_mleader().context.text_style = "Notes";
    std::vector<uint8_t> in = dwg.save_to_memory();
    hdxf_test::assert_false(in.empty(), dwg.last_error());
    std::vector<Record> before = records_of(in);
    std::string balloon_handle, text_handle;
    for (const auto& r : before) {
        if (r[0].second != "MULTILEADER") continue;
        (balloon_handle.empty() ? balloon_handle : text_handle) = r[1].second;
    }
    auto proxies_after = [&](const std::function<void(hdxf::Drawing&)>& change) {
        hdxf::Drawing d = load(in);
        change(d);
        std::vector<uint8_t> out = d.save_to_memory();
        hdxf_test::assert_false(out.empty(), d.last_error());
        std::vector<Record> after = records_of(out);
        return std::pair<std::vector<uint8_t>, std::vector<uint8_t>>{proxy_of(record(after, balloon_handle)),
                                                                     proxy_of(record(after, text_handle))};
    };
    std::vector<uint8_t> balloon_in = proxy_of(record(before, balloon_handle));
    std::vector<uint8_t> text_in = proxy_of(record(before, text_handle));
    hdxf_test::assert_false(balloon_in.empty() || text_in.empty(), "proxy graphics written");

    // Unchanged: as they were
    auto same = proxies_after([](hdxf::Drawing&) {});
    hdxf_test::assert_true(same.first == balloon_in && same.second == text_in, "unchanged graphics kept");

    // The circle bigger: the balloon's graphics drawn anew with it
    auto bigger = proxies_after([](hdxf::Drawing& d) {
        for (auto& be : d.find_block("BALLONG")->entities) {
            if (be.type == hdxf::EntityType::Circle) be.as_circle().radius = 0.8;
        }
    });
    bool radius = false;
    for (const auto& c : decode(bigger.first)) radius = radius || (c.type == 2 && std::fabs(c.numbers[3] - 0.8) < 1e-12);
    hdxf_test::assert_true(radius, "the new radius drawn");
    hdxf_test::assert_true(bigger.second == text_in, "the text multileader as it was");

    // The line removed from the block: drawn without it
    auto fewer = proxies_after([](hdxf::Drawing& d) {
        auto& list = d.find_block("BALLONG")->entities;
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [](const hdxf::Entity& x) { return x.type == hdxf::EntityType::Line; }),
                   list.end());
    });
    hdxf_test::assert_false(fewer.first == balloon_in, "the balloon drawn without the line");
    hdxf_test::assert_true(decode(fewer.first).size() < decode(balloon_in).size(), "one entity less");

    // The style's font changed: the text in the new face
    auto times = proxies_after([](hdxf::Drawing& d) {
        for (auto& s : d.text_styles()) {
            if (s.name == "Notes") {
                s.font_file = "times.ttf";
                s.font_family = "Times New Roman";
            }
        }
    });
    hdxf_test::assert_false(times.second == text_in, "the text drawn anew");
    bool face = false;
    for (const auto& c : decode(times.second)) {
        if (c.type == 38 && c.strings.size() > 1) {
            std::string expected;
            for (char ch : std::string("Times New Roman")) { expected += ch; expected += '\0'; }
            face = face || c.strings[1] == expected;
        }
    }
    hdxf_test::assert_true(face, "Times New Roman");
}

// Breaks (DIMBREAK's gaps): the leader line and the landing drawn in their pieces between the
// gaps, a polyline each — the extents of the pieces only
TEST(mleader_breaks_drawn) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2007);
    hdxf::Entity& e = dwg.add_mleader({{0, 0, 0}, {2, 0, 0}, {4, 0, 0}}, "");
    hdxf::MLeaderData& d = e.as_mleader();
    hdxf::MLeaderLeader& leader = d.context.leaders.front();
    // The line: (0,0) (2,0) to the last point (4,0); a gap on its second segment, another
    // reaching its first segment's end
    hdxf::MLeaderLine& line = leader.lines.front();
    line.breaks.push_back({1, {{{2.5, 0, 0}, {3.0, 0, 0}}}});
    line.breaks.push_back({0, {{{1.5, 0, 0}, {2.0, 0, 0}}}});
    // The landing from (4,0) 0.36 long: a gap at its end
    leader.breaks.push_back({{4.25, 0, 0}, {4.36, 0, 0}});
    hdxf::internal::TextLayout layout(dwg);
    std::vector<Command> c = decode(hdxf::internal::mleader_graphics(dwg, e, tables_of(dwg), layout, true));
    std::vector<std::vector<double>> lines;
    for (const auto& x : c) {
        if (x.type == 6) lines.push_back(std::vector<double>(x.numbers.begin() + 1, x.numbers.end()));
    }
    auto has = [&](std::vector<double> want) {
        for (const auto& l : lines) {
            if (l.size() != want.size()) continue;
            bool same = true;
            for (size_t i = 0; i < l.size(); ++i) same = same && std::fabs(l[i] - want[i]) < 1e-12;
            if (same) return true;
        }
        return false;
    };
    // The arrowhead shortens the line to 0.18
    hdxf_test::assert_true(has({0.18, 0, 0, 1.5, 0, 0}), "the line up to the first gap");
    hdxf_test::assert_true(has({2.0, 0, 0, 2.5, 0, 0}), "between the gaps");
    hdxf_test::assert_true(has({3.0, 0, 0, 4.0, 0, 0}), "after the second gap");
    hdxf_test::assert_true(has({4.0, 0, 0, 4.25, 0, 0}), "the landing up to its gap");
    hdxf_test::assert_eq(static_cast<int>(lines.size()), 4, "four pieces, nothing through the gaps");
    hdxf::internal::Extents ext;
    hdxf::internal::entity_extents(dwg, e, ext);
    hdxf_test::assert_true(std::fabs(ext.max.x - 4.25) < 1e-12, "the extents end at the landing's gap");
}

namespace {

// A block multileader of one block with these entities at (0, 0) → (4, 3): its graphics' commands
// from the block's marker (15004) to its end (the transform popped), and its extents
struct BlockDrawn {
    std::vector<Command> commands;
    hdxf::internal::Extents extents;
    hdxf::Point3 at;                    // the block's position
};

BlockDrawn draw_block_of(hdxf::Drawing& dwg, const std::vector<hdxf::Entity>& entities, const std::string& name) {
    hdxf::Block& b = dwg.add_block(name);
    b.entities = entities;
    hdxf::Entity& e = dwg.add_mleader_block({{0, 0, 0}, {4, 3, 0}}, name);
    BlockDrawn out;
    out.at = e.as_mleader().context.block_position;
    hdxf::internal::TextLayout layout(dwg);
    std::vector<Command> all = decode(hdxf::internal::mleader_graphics(dwg, e, tables_of(dwg), layout, true));
    bool in = false;
    int depth = 0;
    for (const auto& c : all) {
        if (c.type == 19 && c.numbers[0] == 15004) in = true;
        if (!in) continue;
        out.commands.push_back(c);
        if (c.type == 29) ++depth;
        if (c.type == 31 && --depth == 0) break;
    }
    hdxf::internal::entity_extents(dwg, e, out.extents);
    return out;
}

std::vector<int> types_of(const std::vector<Command>& c) {
    std::vector<int> out;
    for (const auto& x : c) out.push_back(x.type);
    return out;
}

std::string hex_of(const Command& c) {
    // A 33's data: its byte count, then the bytes (from the numbers read as 32-bit words)
    std::string out;
    std::vector<uint8_t> b;
    for (size_t i = 0; i < c.numbers.size(); ++i) {
        uint32_t u = static_cast<uint32_t>(static_cast<int32_t>(c.numbers[i]));
        for (int k = 0; k < 4; ++k) b.push_back(static_cast<uint8_t>(u >> (8 * k)));
    }
    if (b.size() < 4) return out;
    uint32_t n = b[0] | (b[1] << 8) | (b[2] << 16) | (static_cast<uint32_t>(b[3]) << 24);
    static const char* digits = "0123456789abcdef";
    for (size_t i = 4; i < 4 + n && i < b.size(); ++i) {
        out += digits[b[i] >> 4];
        out += digits[b[i] & 15];
    }
    return out;
}

// The last entity added to model space, taken out of it (to go into a block)
hdxf::Entity take_last(hdxf::Drawing& dwg) {
    hdxf::Entity e = dwg.entities().back();
    dwg.entities().pop_back();
    return e;
}

} // namespace

// LWPOLYLINE in a multileader's block: AcGi's pline (33) with its bulges and widths in the DWG
// bit stream — byte for byte what ODA's File Converter writes for the same polylines — and in
// the extents its arcs exactly (a chord before: the bulge's arc left out)
TEST(mleader_block_lwpolyline) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2007);
    dwg.add_lwpolyline({{0, 0}, {1, 0}, {1, 1}}, true).as_lwpolyline().vertices[1].bulge = 0.5;
    hdxf::Entity bulged = take_last(dwg);
    BlockDrawn a = draw_block_of(dwg, {bulged}, "BULGE");
    std::vector<int> t = types_of(a.commands);
    hdxf_test::assert_true(t == std::vector<int>({19, 29, 22, 23, 20, 33, 31}), "marker, transform, color, lineweight, fill, LWPLINE");
    hdxf_test::assert_eq(hex_of(a.commands[5]),
                         std::string("040090340c00000000000000000000000000000003000000000000f03f0c000000000003c0fe000000000000380fe0"),
                         "ODA's bytes: closed, bulges");
    // The bulge 0.5 from (1, 0) to (1, 1): the arc reaches x = 1 + r - h right of the chord
    double theta = 4.0 * std::atan(0.5), r = 1.0 / (2.0 * std::sin(theta / 2.0));
    double reach = 0.5 + r - r * std::cos(theta / 2.0) - 0.5;
    hdxf_test::assert_true(std::fabs(a.extents.max.x - (a.at.x + 1.0 + reach)) < 1e-12, "the arc in the extents");

    // A dot: two half circles 0.5 wide — constant width
    hdxf::Entity& dot = dwg.add_lwpolyline({{-0.25, 0}, {0.25, 0}}, true);
    for (auto& v : dot.as_lwpolyline().vertices) {
        v.bulge = 1.0;
        v.start_width = 0.5;
        v.end_width = 0.5;
    }
    hdxf::Entity dotted = take_last(dwg);
    BlockDrawn b = draw_block_of(dwg, {dotted}, "DOT");
    hdxf_test::assert_eq(hex_of(b.commands[5]),
                         std::string("05008000000000000e03f40902000000000000d0bf0000000000000000c00000000000340fc5"),
                         "ODA's bytes: constant width 0.5, bulges 1");
    // Its circle of radius 0.25 (the leader reaches lower and further left)
    hdxf_test::assert_true(std::fabs(b.extents.max.y - (b.at.y + 0.25)) < 1e-12 &&
                               std::fabs(b.extents.max.x - (b.at.x + 0.25)) < 1e-12,
                           "the half circles in the extents");
}

// The other entity types in a multileader's block, each drawn as AutoCAD draws it (a
// dimension block's LINE, POINT and SOLID; an ellipse 44; TEXT as Unicode TEXT2; an INSERT its
// block in its transform), and counted in the extents as an entity of a block is
TEST(mleader_block_entity_types) {
    if (!have_arial()) return;
    FontPaths fonts;
    hdxf::Drawing dwg(hdxf::DxfVersion::R2007);
    hdxf::TextStyle& st = dwg.add_text_style("ArialStyle", "arial.ttf");
    st.font_family = "Arial";
    hdxf::Block& inner = dwg.add_block("INNER");
    dwg.add_circle({0, 0, 0}, 0.2);
    inner.entities.push_back(take_last(dwg));

    std::vector<hdxf::Entity> list;
    dwg.add_line({0, 0, 0}, {1, 0.5, 0});
    list.push_back(take_last(dwg));
    dwg.add_point({0.2, 0.3, 0});
    list.push_back(take_last(dwg));
    dwg.add_ellipse({0, 0, 0}, {1, 0, 0}, 0.5, 0.0, std::acos(-1.0));
    list.push_back(take_last(dwg));
    dwg.add_solid({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0});
    list.push_back(take_last(dwg));
    dwg.add_text({0, -1, 0}, "Ab", 0.3).as_text().style = "ArialStyle";
    list.push_back(take_last(dwg));
    dwg.add_insert("INNER", {3, 0, 0}).as_insert().rotation = 90.0;
    list.push_back(take_last(dwg));
    dwg.add_spline({{0, 0, 0}, {0.3, 1, 0}, {0.7, -1, 0}, {1, 0, 0}});
    list.push_back(take_last(dwg));
    dwg.add_hatch({{0, 0}, {1, 0}, {1, 1}, {0, 1}});
    list.push_back(take_last(dwg));
    BlockDrawn d = draw_block_of(dwg, list, "TYPES");
    std::vector<int> t = types_of(d.commands);
    std::vector<int> want = {19, 29,
                             22, 23, 32,                    // LINE: a polyline with its normal
                             22, 23, 18, 20, 32, 20,        // POINT: Continuous, no fill, one point
                             22, 23, 20, 44, 20,            // ELLIPSE
                             22, 23, 20, 18, 19, 9, 20,     // SOLID: a filled shell
                             22, 23, 38,                    // TEXT
                             22, 23, 29, 22, 23, 20, 2, 20, 31,     // INSERT: its block's circle
                             22, 23, 6,                     // SPLINE: a polyline
                             22, 23, 18, 20, 9,             // HATCH: a filled shell
                             31};
    hdxf_test::assert_true(t == want, "the commands of each type");
    // The ellipse's top (0.5 above its center) and the turned insert's circle (0.2 left of x 3)
    hdxf_test::assert_true(std::fabs(d.extents.max.y - (d.at.y + 1.0)) < 1e-12, "the solid's and hatch's top");
    hdxf_test::assert_true(std::fabs(d.extents.max.x - (d.at.x + 3.2)) < 1e-12, "the inserted circle");
    hdxf_test::assert_true(d.extents.min.y < d.at.y - 1.0, "the text below");
    // The ellipse: center, normal, radii 1 and 0.5, from 0 to π, its axis along x
    for (const auto& c : d.commands) {
        if (c.type != 44) continue;
        hdxf_test::assert_true(c.numbers.size() >= 11 && c.numbers[5] == 1.0 && c.numbers[6] == 1.0 &&
                                   c.numbers[7] == 0.5 && c.numbers[8] == 0.0 && std::fabs(c.numbers[9] - std::acos(-1.0)) < 1e-15 &&
                                   c.numbers[10] == 0.0,
                               "the ellipse's data");
    }
}

// A constant attribute is part of its block: drawn with it, with no value of the multileader's
TEST(mleader_block_constant_attribute) {
    if (!have_arial()) return;
    FontPaths fonts;
    hdxf::Drawing dwg(hdxf::DxfVersion::R2007);
    hdxf::TextStyle& st = dwg.add_text_style("ArialStyle", "arial.ttf");
    st.font_family = "Arial";
    hdxf::Entity def;
    def.type = hdxf::EntityType::Attdef;
    hdxf::AttdefData a;
    a.tag = "K";
    a.default_value = "Konst";
    a.height = 0.2;
    a.flags = 2;
    a.style = "ArialStyle";
    def.data = a;
    BlockDrawn d = draw_block_of(dwg, {def}, "CONSTANT");
    hdxf_test::assert_true(dwg.entities().back().as_mleader().attributes.empty(), "no value of its own");
    std::vector<int> t = types_of(d.commands);
    hdxf_test::assert_true(t == std::vector<int>({19, 29, 22, 23, 38, 31}), "its text in the block");
}

// A multileader's extents in a transform that turns it (45°): its leader, arrowhead and landing
// mapped point by point, not its box (whose corner reaches x 3.08)
TEST(mleader_extents_mapped) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2007);
    hdxf::Entity& e = dwg.add_mleader({{0, 0, 0}, {4, 3, 0}}, "");
    double c = std::sqrt(0.5);
    std::array<double, 16> turn = {c, -c, 0, 0, c, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    hdxf::internal::TextLayout layout(dwg);
    hdxf::internal::Extents ext;
    hdxf::internal::mleader_extents(dwg, e, layout, turn, [](const hdxf::Entity&, const std::array<double, 16>&) {}, ext);
    // The landing's end (4.36, 3) turned: x (4.36 - 3)·√½, y (4.36 + 3)·√½
    hdxf_test::assert_true(std::fabs(ext.max.x - 1.36 * c) < 1e-12, "the landing's end the rightmost");
    hdxf_test::assert_true(std::fabs(ext.max.y - 7.36 * c) < 1e-12, "and the highest");
}

// The extents of a text multileader count its text by the font's cell: from the pen to the
// advance, from the descent to the ascent (Arial: usWinDescent 434 and usWinAscent 1854 of
// the 1466 of its text height — AutoCAD's $EXTMIN/$EXTMAX at multileaders, see
// test/data/README.md)
TEST(mleader_extents_text_cells) {
    if (!have_arial()) return;
    FontPaths fonts;
    hdxf::Drawing dwg(hdxf::DxfVersion::R2007);
    hdxf::TextStyle& st = dwg.add_text_style("ArialStyle", "arial.ttf");
    st.font_family = "Arial";
    st.font_flags = 34;
    hdxf::Object& style = dwg.add_mleader_style("Notes");
    style.as_mleader_style().text_style = "ArialStyle";
    style.as_mleader_style().text_height = 2.0;
    // From above and the left: the text under the landing's end decides the bottom and the right
    hdxf::Entity& e = dwg.add_mleader({{0, 10, 0}, {5, 5, 0}}, "Hg", "Notes");
    hdxf::internal::TextLayout layout(dwg);
    std::vector<Command> c = decode(hdxf::internal::mleader_graphics(dwg, e, tables_of(dwg), layout, true));
    double pen_x = 0, base_y = 0;
    for (const auto& x : c) {
        if (x.type == 38) { pen_x = x.numbers[0]; base_y = x.numbers[1]; }
    }
    hdxf::internal::Extents ext;
    hdxf::internal::entity_extents(dwg, e, ext);
    double h = 2.0;
    // H and g advance 1479 and 1139 of 2048; the text height is 1466 of them
    hdxf_test::assert_true(std::fabs(ext.max.x - (pen_x + (1479.0 + 1139.0) / 1466.0 * h)) < 1e-12, "the advance");
    hdxf_test::assert_true(std::fabs(ext.min.y - (base_y - 434.0 / 1466.0 * h)) < 1e-12, "the descent");
    hdxf_test::assert_true(std::fabs(ext.max.y - 10.0) < 1e-12, "the arrowhead's tip above");
}

// Invisible leaders (170 0) with a line visible by its own type (2010, B47): that line is drawn,
// arrowhead and landing with it, and counts in the extents; without the override the content
// only (Codex review of phase 4)
TEST(mleader_line_visible_by_own_type) {
    for (bool own : {true, false}) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2010);
        hdxf::Entity& e = dwg.add_mleader({{0, 0, 0}, {4, 3, 0}}, "");
        hdxf::MLeaderData& d = e.as_mleader();
        d.leader_type = 0;
        if (own) {
            hdxf::MLeaderLine& line = d.context.leaders.at(0).lines.at(0);
            line.leader_type = 1;
            line.override_flags = 1;
        }
        hdxf::internal::TextLayout layout(dwg);
        std::vector<Command> c = decode(hdxf::internal::mleader_graphics(dwg, e, tables_of(dwg), layout, true));
        bool line_drawn = false, landing = false;
        for (size_t i = 0; i + 1 < c.size(); ++i) {
            if (c[i].type != 19) continue;
            // To the leader's last point (4, 3)
            const std::vector<double>& n = c[i + 1].numbers;
            if (c[i].numbers[0] == 5001) line_drawn = c[i + 1].type == 6 && n.size() >= 7 && n[n.size() - 3] == 4.0 &&
                                                      n[n.size() - 2] == 3.0;
            if (c[i].numbers[0] == 10001) landing = c[i + 1].type == 6;
        }
        hdxf_test::assert_eq(line_drawn ? 1 : 0, own ? 1 : 0, "the line drawn");
        hdxf_test::assert_eq(landing ? 1 : 0, own ? 1 : 0, "its landing");
        hdxf::internal::Extents ext;
        hdxf::internal::entity_extents(dwg, e, ext);
        bool tip = !ext.empty() && ext.min.x == 0.0 && ext.min.y == 0.0;
        hdxf_test::assert_eq(tip ? 1 : 0, own ? 1 : 0, "the arrowhead's tip in the extents");
    }
}

// A line's own color (2010, B47) with an arrowhead block drawn ByBlock: the block's parts and the
// line after it in the line's color, not the leader's (Codex review of phase 4)
TEST(mleader_line_color_through_arrow_block) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2010);
    hdxf::Block& arrow = dwg.add_block("PIL");
    hdxf::Entity part;
    part.type = hdxf::EntityType::Line;
    part.data = hdxf::LineData{{-1, 0, 0}, {0, 0, 0}};
    part.props.color = hdxf::Color::by_block();
    arrow.entities.push_back(part);
    hdxf::Entity& e = dwg.add_mleader({{0, 0, 0}, {4, 3, 0}}, "");
    hdxf::MLeaderData& d = e.as_mleader();
    d.leader_color = hdxf::CmColor::from_color(hdxf::Color::from_aci(5));
    d.arrow_block = "PIL";
    d.arrowheads.clear();
    hdxf::MLeaderLine& line = d.context.leaders.at(0).lines.at(0);
    line.color = hdxf::CmColor::from_color(hdxf::Color::from_aci(1));
    line.override_flags = 2;
    double red = hdxf::CmColor::from_color(hdxf::Color::from_aci(1)).raw;
    hdxf::internal::TextLayout layout(dwg);
    std::vector<Command> c = decode(hdxf::internal::mleader_graphics(dwg, e, tables_of(dwg), layout, true));
    double color = 0;
    bool in_block = false, shaft_marker = false, block_part = false, shaft = false;
    for (const auto& x : c) {
        if (x.type == 22) color = x.numbers[0];
        if (x.type == 29) in_block = true;
        if (x.type == 31) in_block = false;
        bool line_command = x.type == 6 || x.type == 32;
        if (in_block && line_command) {
            hdxf_test::assert_true(color == red, "the arrowhead's ByBlock part in the line's color");
            block_part = true;
        }
        if (x.type == 19) shaft_marker = x.numbers[0] == 5001;
        if (shaft_marker && line_command) {
            hdxf_test::assert_true(color == red, "the line in its color after the arrowhead");
            shaft = true;
            shaft_marker = false;
        }
    }
    hdxf_test::assert_true(block_part && shaft, "arrowhead block and line drawn");
}
