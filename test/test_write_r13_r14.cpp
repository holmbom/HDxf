#include "test_framework.hpp"
#include "dxf_structure.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Writing DXF R13 (AC1012) and R14 (AC1014): version choice, header, binary groups and the base
// structure (plan phase 7, steps 7.2 – 7.5, B86, B88, B89). No golden files: what is checked is
// the structure and the order and codes AutoCAD's own R13 and R14 files have
// (ezdxf/small_r13.dxf, bin_dxf_r13.dxf, small_r14.dxf, bin_dxf_r14.dxf).
// ---------------------------------------------------------------------------

namespace {

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::internal::BinaryCodeWidth;
using hdxf::internal::GroupCode;
using hdxf::internal::GroupReader;

struct Group {
    int code;
    std::string value;
};

const DxfVersion older_versions[] = {DxfVersion::R13, DxfVersion::R14};
const DxfFormat both_formats[] = {DxfFormat::Ascii, DxfFormat::Binary};

std::string name_of(DxfVersion v) { return v == DxfVersion::R13 ? "R13" : "R14"; }
std::string name_of(DxfFormat f) { return f == DxfFormat::Ascii ? "ASCII" : "binary"; }

std::vector<uint8_t> save(hdxf::Drawing& dwg, DxfFormat format, const std::string& what) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), what + ": " + dwg.last_error());
    return out;
}

// Every group of a file, ASCII or binary, to its EOF; numbers without their alignment
std::vector<Group> groups_of(const std::vector<uint8_t>& data, BinaryCodeWidth* width = nullptr,
                             bool* binary = nullptr) {
    std::istringstream in(std::string(data.begin(), data.end()), std::ios::binary);
    GroupReader reader(in);
    reader.detect_format();
    if (width) *width = reader.code_width();
    if (binary) *binary = reader.binary();
    std::vector<Group> out;
    GroupCode gc;
    while (reader.read(gc)) out.push_back({gc.code, dxf_structure::number_text(gc.code, gc.value)});
    hdxf_test::assert_true(reader.error().empty(), "reading the file: " + reader.error());
    return out;
}

// The records, as dxf_structure::records_of() makes them for ASCII
std::vector<dxf_structure::Rec> records_of(const std::vector<Group>& groups) {
    std::vector<dxf_structure::Rec> out;
    std::string section;
    for (const auto& g : groups) {
        if (g.code == 0) out.push_back({section, {}});
        if (out.empty()) continue;
        out.back().groups.push_back({g.code, g.value});
        if (g.code == 2 && out.back().type() == "SECTION") section = g.value;
        if (g.code == 0 && g.value == "ENDSEC") section.clear();
    }
    return out;
}

// The header's variables as (name, groups)
std::vector<std::pair<std::string, std::vector<Group>>> header_of(const std::vector<Group>& groups) {
    std::vector<std::pair<std::string, std::vector<Group>>> out;
    bool in_header = false;
    for (size_t i = 1; i < groups.size(); ++i) {
        if (groups[i].code == 2 && groups[i].value == "HEADER" && groups[i - 1].value == "SECTION") {
            in_header = true;
            continue;
        }
        if (!in_header) continue;
        if (groups[i].code == 0 && groups[i].value == "ENDSEC") break;
        if (groups[i].code == 9) out.push_back({groups[i].value, {}});
        else if (!out.empty()) out.back().second.push_back(groups[i]);
    }
    return out;
}

std::vector<std::string> header_names(const std::vector<Group>& groups) {
    std::vector<std::string> names;
    for (const auto& [name, values] : header_of(groups)) names.push_back(name);
    return names;
}

std::string header_value(const std::vector<Group>& groups, const std::string& name, int code) {
    for (const auto& [n, values] : header_of(groups)) {
        if (n != name) continue;
        for (const auto& g : values) if (g.code == code) return g.value;
    }
    return "<none>";
}

// R13's header in the order of AutoCAD's own R13 file (libre/r13_v.dxf, format profile 3.2: 162 names,
// $BLIPMODE after $LIMCHECK, $SAVEIMAGES last)
std::vector<std::string> r13_names() {
    const char* names =
        "ACADVER DWGCODEPAGE INSBASE EXTMIN EXTMAX LIMMIN LIMMAX ORTHOMODE REGENMODE FILLMODE QTEXTMODE MIRRTEXT "
        "DRAGMODE LTSCALE OSMODE ATTMODE TEXTSIZE TRACEWID TEXTSTYLE CLAYER CELTYPE CECOLOR CELTSCALE DELOBJ "
        "DISPSILH DIMSCALE DIMASZ DIMEXO DIMDLI DIMRND DIMDLE DIMEXE DIMTP DIMTM DIMTXT DIMCEN "
        "DIMTSZ DIMTOL DIMLIM DIMTIH DIMTOH DIMSE1 DIMSE2 DIMTAD DIMZIN DIMBLK DIMASO DIMSHO "
        "DIMPOST DIMAPOST DIMALT DIMALTD DIMALTF DIMLFAC DIMTOFL DIMTVP DIMTIX DIMSOXD DIMSAH DIMBLK1 "
        "DIMBLK2 DIMSTYLE DIMCLRD DIMCLRE DIMCLRT DIMTFAC DIMGAP DIMJUST DIMSD1 DIMSD2 DIMTOLJ DIMTZIN "
        "DIMALTZ DIMALTTZ DIMFIT DIMUPT DIMUNIT DIMDEC DIMTDEC DIMALTU DIMALTTD DIMTXSTY DIMAUNIT LUNITS "
        "LUPREC SKETCHINC FILLETRAD AUNITS AUPREC MENU ELEVATION PELEVATION THICKNESS LIMCHECK BLIPMODE CHAMFERA CHAMFERB "
        "CHAMFERC CHAMFERD SKPOLY TDCREATE TDUPDATE TDINDWG TDUSRTIMER USRTIMER ANGBASE ANGDIR PDMODE PDSIZE "
        "PLINEWID COORDS SPLFRAME SPLINETYPE SPLINESEGS ATTDIA ATTREQ HANDLING HANDSEED SURFTAB1 SURFTAB2 SURFTYPE "
        "SURFU SURFV UCSNAME UCSORG UCSXDIR UCSYDIR PUCSNAME PUCSORG PUCSXDIR PUCSYDIR USERI1 USERI2 "
        "USERI3 USERI4 USERI5 USERR1 USERR2 USERR3 USERR4 USERR5 WORLDVIEW SHADEDGE SHADEDIF TILEMODE "
        "MAXACTVP PINSBASE PLIMCHECK PEXTMIN PEXTMAX PLIMMIN PLIMMAX UNITMODE VISRETAIN PLINEGEN PSLTSCALE TREEDEPTH "
        "PICKSTYLE CMLSTYLE CMLJUST CMLSCALE SAVEIMAGES";
    std::vector<std::string> out;
    std::istringstream in(names);
    std::string n;
    while (in >> n) out.push_back("$" + n);
    return out;
}

// R14's: $ACADMAINTVER after $ACADVER, no $SAVEIMAGES, $PROXYGRAPHICS and $MEASUREMENT last
std::vector<std::string> r14_names() {
    std::vector<std::string> out;
    for (const auto& n : r13_names()) {
        if (n == "$SAVEIMAGES") continue;
        out.push_back(n);
        if (n == "$ACADVER") out.push_back("$ACADMAINTVER");
    }
    out.push_back("$PROXYGRAPHICS");
    out.push_back("$MEASUREMENT");
    return out;
}

const std::vector<std::string>& expected_names(DxfVersion v) {
    static const std::vector<std::string> r13 = r13_names();
    static const std::vector<std::string> r14 = r14_names();
    return v == DxfVersion::R13 ? r13 : r14;
}

// The records of a section
std::vector<const dxf_structure::Rec*> in_section(const std::vector<dxf_structure::Rec>& recs,
                                                  const std::string& section) {
    std::vector<const dxf_structure::Rec*> out;
    for (const auto& r : recs) {
        if (r.section == section && r.type() != "SECTION" && r.type() != "ENDSEC") out.push_back(&r);
    }
    return out;
}

std::vector<std::string> types_in(const std::vector<dxf_structure::Rec>& recs, const std::string& section) {
    std::vector<std::string> out;
    for (const auto* r : in_section(recs, section)) out.push_back(r->type());
    return out;
}

bool has(const std::vector<std::string>& list, const std::string& what) {
    return std::find(list.begin(), list.end(), what) != list.end();
}

// A record's value of a code from the subclass `marker` on
std::string value_after(const dxf_structure::Rec& r, const std::string& marker, int code) {
    bool in = false;
    for (const auto& g : r.groups) {
        if (g.first == 100) in = g.second == marker;
        if (in && g.first == code) return g.second;
    }
    return {};
}

bool has_code(const dxf_structure::Rec& r, int code) {
    for (const auto& g : r.groups) if (g.first == code) return true;
    return false;
}

const dxf_structure::Rec* record_named(const std::vector<dxf_structure::Rec>& recs, const std::string& section,
                                       const std::string& type, const std::string& name) {
    for (const auto* r : in_section(recs, section)) {
        if (r->type() == type && r->value(2) == name) return r;
    }
    return nullptr;
}

std::string groups_text(const dxf_structure::Rec& r) {
    std::string text;
    for (const auto& g : r.groups) text += std::to_string(g.first) + "=" + g.second + ";";
    return text;
}

std::filesystem::path temp_file(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

std::string read_all(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::filesystem::path external(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / "ezdxf" / name;
}

std::string handseed_of(const hdxf::Drawing& dwg) {
    const hdxf::HeaderVar* seed = dwg.find_header("$HANDSEED");
    return seed ? seed->values[0].second : "";
}

} // namespace

// ---------------------------------------------------------------------------
// 7.2: the versions in every I/O call (B86)
// ---------------------------------------------------------------------------

TEST(write_r13_r14_versions_in_all_io_calls) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing dwg(v);
            hdxf_test::assert_true(dwg.version() == v, what + ": version");
            dwg.add_line({0, 0, 0}, {10, 5, 0});

            // save_to_memory
            std::vector<uint8_t> memory = save(dwg, format, what + " save_to_memory");
            std::vector<Group> groups = groups_of(memory);
            hdxf_test::assert_eq(header_value(groups, "$ACADVER", 1),
                                 std::string(hdxf::acadver_string(v)), what + ": $ACADVER");

            // save_as, then save() to the same file
            std::filesystem::path path = temp_file("hdxf_r13_r14_io.dxf");
            std::filesystem::remove(path);
            hdxf_test::assert_true(dwg.save_as(path, format), what + " save_as: " + dwg.last_error());
            hdxf_test::assert_true(std::filesystem::exists(path), what + ": file");
            hdxf_test::assert_true(dwg.save(format), what + " save: " + dwg.last_error());

            // Read back, from the file and from memory: the same version, the same line
            hdxf::Drawing from_file;
            hdxf_test::assert_true(from_file.load(path), what + " load: " + from_file.last_error());
            hdxf_test::assert_true(from_file.version() == v, what + ": version read");
            hdxf_test::assert_eq(static_cast<int>(from_file.entities().size()), 1, what + ": entities");
            hdxf::Drawing from_memory;
            hdxf_test::assert_true(from_memory.load_from_memory(memory.data(), memory.size()),
                                   what + " load_from_memory: " + from_memory.last_error());
            hdxf_test::assert_true(from_memory.version() == v, what + ": version read from memory");
            hdxf_test::assert_eq(from_memory.entities().at(0).as_line().end.x, 10.0, what + ": line end");
            std::filesystem::remove(path);
        }
    }
}

// A new drawing of R12 (or one read from an R12 file) gets R13/R14 by set_version()
TEST(write_r13_r14_set_version_on_a_new_drawing) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing built(v);
            built.add_line({0, 0, 0}, {1, 1, 0});
            hdxf::Drawing dwg;      // R12
            dwg.add_line({0, 0, 0}, {1, 1, 0});
            dwg.set_version(v);
            hdxf_test::assert_true(dwg.version() == v, what);
            std::vector<uint8_t> a = save(dwg, format, what);
            if (format == DxfFormat::Ascii) dxf_structure::check_structure(a, what);
            // The same file as a drawing made as that version
            std::vector<Group> ga = groups_of(a);
            std::vector<Group> gb = groups_of(save(built, format, what + " built"));
            hdxf_test::assert_eq(static_cast<int>(ga.size()), static_cast<int>(gb.size()), what + ": groups");
            for (size_t i = 0; i < ga.size() && i < gb.size(); ++i) {
                hdxf_test::assert_true(ga[i].code == gb[i].code && ga[i].value == gb[i].value,
                                       what + ": group " + std::to_string(i));
            }
        }
    }
}

// R12 as before, also after the older structure was there
TEST(write_r13_r14_r12_unchanged_by_the_older_structure) {
    hdxf::Drawing dwg(DxfVersion::R13);
    dwg.add_line({0, 0, 0}, {1, 0, 0});
    dwg.set_version(DxfVersion::R12);
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    std::string s = hdxf_test::lf_text(out);
    hdxf_test::assert_true(s.find("AC1009") != std::string::npos, "R12");
    hdxf_test::assert_true(s.find("*MODEL_SPACE") == std::string::npos &&
                           s.find("*Model_Space") == std::string::npos, "no model space block in R12");
    hdxf_test::assert_true(s.find("AcDbEntity") == std::string::npos, "no subclass markers in R12");
}

// A drawing read from an R12 file written as R13 or R14: a conversion upwards, as to 2000
TEST(write_r13_r14_from_a_drawing_read_from_r12) {
    hdxf::Drawing src;
    src.add_line({0, 0, 0}, {2, 0, 0});
    src.add_circle({1, 1, 0}, 3);
    std::vector<uint8_t> r12 = save(src, DxfFormat::Ascii, "R12");
    for (DxfVersion v : older_versions) {
        hdxf::Drawing dwg;
        hdxf_test::assert_true(dwg.load_from_memory(r12.data(), r12.size()), dwg.last_error());
        hdxf_test::assert_true(dwg.version() == DxfVersion::R12, "read as R12");
        dwg.set_version(v);
        for (DxfFormat format : both_formats) {
            std::string what = "R12 file as " + name_of(v) + " " + name_of(format);
            std::vector<uint8_t> out = save(dwg, format, what);
            std::vector<Group> groups = groups_of(out);
            hdxf_test::assert_eq(header_value(groups, "$ACADVER", 1), std::string(hdxf::acadver_string(v)), what);
            // R12's own variables ($VIEWSIZE, $SNAPMODE …) are not in the R13/R14 header
            std::vector<std::string> names = header_names(groups);
            hdxf_test::assert_false(has(names, "$VIEWSIZE") || has(names, "$FASTZOOM") || has(names, "$GRIDMODE"),
                                    what + ": R12 variables");
            if (format == DxfFormat::Ascii) dxf_structure::check_structure(out, what);
        }
    }
}

// ---------------------------------------------------------------------------
// 7.3, 7.4: the header (B88)
// ---------------------------------------------------------------------------

TEST(write_r13_r14_header_names_in_order) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing dwg(v);
            std::vector<std::string> names = header_names(groups_of(save(dwg, format, what)));
            const std::vector<std::string>& expected = expected_names(v);
            hdxf_test::assert_eq(static_cast<int>(expected.size()), v == DxfVersion::R13 ? 162 : 164, what + ": the list");
            hdxf_test::assert_eq(static_cast<int>(names.size()), static_cast<int>(expected.size()), what + ": count");
            for (size_t i = 0; i < expected.size() && i < names.size(); ++i) {
                hdxf_test::assert_eq(names[i], expected[i], what + ": variable " + std::to_string(i));
            }
        }
    }
}

TEST(write_r13_r14_header_codes_and_values) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing dwg(v);
            std::vector<Group> g = groups_of(save(dwg, format, what));
            // The codes AutoCAD's R13 and R14 files have
            hdxf_test::assert_eq(header_value(g, "$DIMBLK", 1), "", what + ": $DIMBLK is 1");
            hdxf_test::assert_eq(header_value(g, "$DIMBLK1", 1), "", what + ": $DIMBLK1 is 1");
            hdxf_test::assert_eq(header_value(g, "$DIMTXSTY", 7).empty() ? "empty" : "ok", "ok", what + ": $DIMTXSTY is 7");
            hdxf_test::assert_eq(header_value(g, "$TEXTSTYLE", 7).empty() ? "empty" : "ok", "ok", what + ": $TEXTSTYLE is 7");
            hdxf_test::assert_eq(header_value(g, "$CMLSTYLE", 2), "STANDARD", what + ": $CMLSTYLE is 2");
            hdxf_test::assert_eq(header_value(g, "$DIMSTYLE", 2).empty() ? "empty" : "ok", "ok", what + ": $DIMSTYLE is 2");
            hdxf_test::assert_eq(header_value(g, "$DWGCODEPAGE", 3), "ANSI_1252", what + ": $DWGCODEPAGE");
            // Older dimension variables, and the ones R13 and R14 have of their own
            hdxf_test::assert_eq(header_value(g, "$DIMUNIT", 70), "2", what + ": $DIMUNIT");
            hdxf_test::assert_eq(header_value(g, "$DIMFIT", 70), "3", what + ": $DIMFIT");
            hdxf_test::assert_eq(header_value(g, "$HANDLING", 70), "1", what + ": $HANDLING");
            hdxf_test::assert_eq(header_value(g, "$TREEDEPTH", 70), "3020", what + ": $TREEDEPTH");
            hdxf_test::assert_eq(header_value(g, "$PICKSTYLE", 70), "1", what + ": $PICKSTYLE");
            hdxf_test::assert_eq(header_value(g, "$DRAGMODE", 70), "2", what + ": $DRAGMODE");
            hdxf_test::assert_false(header_value(g, "$HANDSEED", 5) == "<none>", what + ": $HANDSEED is 5");
            // Maintenance: none in R13, 0 in a new R14 drawing (D3); $SAVEIMAGES in R13 only
            hdxf_test::assert_eq(header_value(g, "$ACADMAINTVER", 70), v == DxfVersion::R13 ? "<none>" : "0", what);
            // AutoCAD's own values where every R13/R14 file has the same (D2)
            hdxf_test::assert_eq(header_value(g, "$MIRRTEXT", 70), "1", what + ": $MIRRTEXT");
            hdxf_test::assert_eq(header_value(g, "$PDMODE", 70), "0", what + ": $PDMODE");
            hdxf_test::assert_eq(header_value(g, "$BLIPMODE", 70), "0", what + ": $BLIPMODE");
            hdxf_test::assert_eq(header_value(g, "$MENU", 1), ".", what + ": $MENU");
            // An empty drawing: the extents of AutoCAD's empty drawings
            for (const char* name : {"$EXTMIN", "$PEXTMIN"}) {
                hdxf_test::assert_eq(std::atof(header_value(g, name, 10).c_str()), 1e20, what + ": " + name);
            }
            for (const char* name : {"$EXTMAX", "$PEXTMAX"}) {
                hdxf_test::assert_eq(std::atof(header_value(g, name, 30).c_str()), -1e20, what + ": " + name);
            }
            hdxf_test::assert_eq(header_value(g, "$SAVEIMAGES", 70), v == DxfVersion::R13 ? "1" : "<none>", what);
            hdxf_test::assert_eq(header_value(g, "$PROXYGRAPHICS", 70), v == DxfVersion::R13 ? "<none>" : "1", what);
            hdxf_test::assert_eq(header_value(g, "$MEASUREMENT", 70), v == DxfVersion::R13 ? "<none>" : "1", what);
            // Nothing 2000 has: lineweight and plot style, newer dimension variables, GUIDs, units
            for (const char* name : {"$DIMLUNIT", "$DIMATFIT", "$DIMLWD", "$DIMLWE", "$DIMTMOVE", "$CELWEIGHT",
                                     "$ENDCAPS", "$JOINSTYLE", "$LWDISPLAY", "$INSUNITS", "$PSTYLEMODE",
                                     "$CEPSNTYPE", "$FINGERPRINTGUID", "$VERSIONGUID", "$EXTNAMES", "$XEDIT",
                                     "$UCSBASE", "$UCSORTHOREF", "$TDUCREATE", "$HYPERLINKBASE", "$LASTSAVEDBY"}) {
                hdxf_test::assert_eq(static_cast<int>(has(header_names(g), name)), 0, what + ": " + name);
            }
        }
    }
}

// Values the program set are written in the variable's place; one R12 has and R13/R14 not is not written
TEST(write_r13_r14_header_override) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing dwg(v);
            dwg.set_header_int("$LUNITS", 70, 3);
            dwg.set_header_int("$DIMFIT", 70, 1);
            dwg.set_header_int("$DIMUNIT", 70, 4);
            dwg.set_header_double("$TEXTSIZE", 40, 0.2);
            dwg.set_header_string("$TEXTSTYLE", 7, "Notes");
            dwg.set_header_int("$VIEWSIZE", 70, 3);          // R12's: not in R13/R14
            std::vector<Group> g = groups_of(save(dwg, format, what));
            hdxf_test::assert_eq(header_value(g, "$LUNITS", 70), "3", what);
            hdxf_test::assert_eq(header_value(g, "$DIMFIT", 70), "1", what);
            hdxf_test::assert_eq(header_value(g, "$DIMUNIT", 70), "4", what);
            hdxf_test::assert_eq(std::atof(header_value(g, "$TEXTSIZE", 40).c_str()), 0.2, what + ": $TEXTSIZE");
            hdxf_test::assert_eq(header_value(g, "$TEXTSTYLE", 7), "Notes", what);
            hdxf_test::assert_eq(header_value(g, "$VIEWSIZE", 70), "<none>", what);
            // The order is still the version's
            hdxf_test::assert_true(header_names(g) == expected_names(v), what + ": order");
        }
    }
}

// A header variable only 2000 or later has is a version error before anything is written (B98), as is
// R14's $PROXYGRAPHICS in R13
TEST(write_r13_r14_header_variable_of_a_later_version_is_an_error) {
    for (DxfVersion v : older_versions) {
        hdxf::Drawing dwg(v);
        dwg.add_line({0, 0, 0}, {1, 0, 0});
        dwg.set_header_int("$CELWEIGHT", 370, 50);
        std::vector<uint8_t> out = dwg.save_to_memory();
        hdxf_test::assert_true(out.empty(), name_of(v) + ": saved");
        hdxf_test::assert_true(dwg.last_error().find("$CELWEIGHT requires DXF 2000 or later") != std::string::npos,
                               name_of(v) + ": " + dwg.last_error());
    }
    hdxf::Drawing r13(DxfVersion::R13);
    r13.set_header_int("$PROXYGRAPHICS", 70, 0);
    hdxf_test::assert_true(r13.save_to_memory().empty(), "R13 with $PROXYGRAPHICS");
    hdxf_test::assert_true(r13.last_error().find("$PROXYGRAPHICS requires DXF R14 or later") != std::string::npos,
                           r13.last_error());
}

// $DIMUNIT and $DIMFIT of a new drawing are the ones of the dimension style $DIMSTYLE names (B91);
// the program's own header value is kept (header_override)
TEST(write_r13_r14_header_dimunit_dimfit_follow_the_current_style) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing dwg(v);
            hdxf::DimStyle& plan = dwg.add_dim_style("Plan");
            plan.dimlunit = 4;
            plan.dimfrac = 2;       // DIMUNIT 6
            plan.dimatfit = 3;
            plan.dimtmove = 1;      // DIMFIT 4
            // Not the current style: the header is the Standard style's
            std::vector<Group> g = groups_of(save(dwg, format, what));
            hdxf_test::assert_eq(header_value(g, "$DIMUNIT", 70), "2", what + ": Standard is current");
            hdxf_test::assert_eq(header_value(g, "$DIMFIT", 70), "3", what);
            dwg.set_header_string("$DIMSTYLE", 2, "PLAN");
            g = groups_of(save(dwg, format, what));
            hdxf_test::assert_eq(header_value(g, "$DIMUNIT", 70), "6", what + ": Plan is current");
            hdxf_test::assert_eq(header_value(g, "$DIMFIT", 70), "4", what);
            hdxf_test::assert_true(header_names(g) == expected_names(v), what + ": order");
        }
    }
}

// A dimension style without an older form refuses the save before anything is changed (B98)
TEST(write_r13_r14_dimstyle_without_an_older_form_changes_nothing) {
    for (DxfVersion v : older_versions) {
        std::string what = name_of(v);
        hdxf::Drawing dwg(v);
        dwg.add_line({0, 0, 0}, {1, 0, 0});
        dwg.add_dim_style("Newer").dimdsep = ',';
        std::string handseed = handseed_of(dwg);
        size_t objects = dwg.objects().size();
        hdxf_test::assert_true(dwg.save_to_memory().empty(), what + ": saved");
        hdxf_test::assert_true(dwg.last_error().find("DIMDSEP requires DXF 2000 or later") != std::string::npos,
                               what + ": " + dwg.last_error());
        hdxf_test::assert_eq(handseed_of(dwg), handseed, what + ": $HANDSEED");
        hdxf_test::assert_eq(static_cast<int>(dwg.objects().size()), static_cast<int>(objects), what + ": objects");
        for (const auto& e : dwg.entities()) hdxf_test::assert_true(e.props.handle.empty(), what + ": handle kept");
        for (const auto& d : dwg.dim_styles()) hdxf_test::assert_true(d.handle.empty(), what + ": style handle kept");
    }
}

// The header of a drawing made as a later version and set to R13/R14 has only the older variables
TEST(write_r13_r14_header_of_a_later_drawing) {
    hdxf::Drawing dwg(DxfVersion::R2018);
    dwg.add_line({0, 0, 0}, {1, 0, 0});
    for (DxfVersion v : older_versions) {
        dwg.set_version(v);
        std::vector<std::string> names = header_names(groups_of(save(dwg, DxfFormat::Ascii, name_of(v))));
        hdxf_test::assert_true(names == expected_names(v), name_of(v) + ": header of a 2018 drawing");
    }
}

// ---------------------------------------------------------------------------
// 7.4: binary DXF with the common codec
// ---------------------------------------------------------------------------

TEST(write_r13_r14_binary_two_byte_codes) {
    for (DxfVersion v : older_versions) {
        std::string what = name_of(v);
        hdxf::Drawing dwg(v);
        hdxf::Entity& line = dwg.add_line({0, 0, 0}, {10, 5, 0});
        // XDATA of every kind of value: a string, binary chunk (1004), handle (1005), real,
        // negative int16 (1070) and int32 (1071)
        line.props.xdata.push_back({"HDXF_TEST", {{1000, "text"}, {1004, "0A0B0C"}, {1005, "2A"},
                                                   {1040, "1.5"}, {1070, "-5"}, {1071, "70000"}}});
        hdxf::DimStyle& style = dwg.add_dim_style("D1");
        style.dimjust = 2;         // 280
        style.dimsd1 = 1;          // 281
        std::vector<uint8_t> out = save(dwg, DxfFormat::Binary, what);

        BinaryCodeWidth width = BinaryCodeWidth::OneByte;
        bool binary = false;
        std::vector<Group> groups = groups_of(out, &width, &binary);
        hdxf_test::assert_true(binary, what + ": binary");
        hdxf_test::assert_true(width == BinaryCodeWidth::TwoBytes, what + ": two-byte codes");
        // The sentinel, then 0 SECTION as two bytes 0 0, and the file ends with 0 EOF
        std::string raw(out.begin(), out.end());
        hdxf_test::assert_eq(raw.substr(0, 18), "AutoCAD Binary DXF", what + ": sentinel");
        hdxf_test::assert_eq(raw.substr(18, 4), std::string("\r\n\x1a\0", 4), what + ": sentinel end");
        hdxf_test::assert_eq(raw.substr(22, 2), std::string("\0\0", 2), what + ": first code");
        hdxf_test::assert_true(!groups.empty() && groups.front().code == 0 && groups.front().value == "SECTION", what);
        hdxf_test::assert_true(groups.back().code == 0 && groups.back().value == "EOF", what + ": EOF");

        // The groups of the XDATA, with the type of their code
        bool found = false;
        for (size_t i = 0; i + 6 < groups.size(); ++i) {
            if (groups[i].code != 1001 || groups[i].value != "HDXF_TEST") continue;
            found = true;
            hdxf_test::assert_eq(groups[i + 1].value, "text", what);
            hdxf_test::assert_true(groups[i + 2].code == 1004 && groups[i + 2].value == "0A0B0C",
                                   what + ": 1004 " + std::to_string(groups[i + 2].code) + " " + groups[i + 2].value);
            hdxf_test::assert_true(groups[i + 3].code == 1005 && groups[i + 3].value == "2A", what + ": 1005");
            hdxf_test::assert_true(groups[i + 4].code == 1040 && std::atof(groups[i + 4].value.c_str()) == 1.5, what);
            hdxf_test::assert_true(groups[i + 5].code == 1070 && groups[i + 5].value == "-5", what + ": negative int16");
            hdxf_test::assert_true(groups[i + 6].code == 1071 && groups[i + 6].value == "70000", what + ": int32");
        }
        hdxf_test::assert_true(found, what + ": XDATA");

        // The bytes: a code above 255 is its two bytes, 1070 -5 an int16, 1071 an int32, 1004 a
        // length and its bytes, 280 an int16 (not a byte)
        auto contains = [&](const std::string& pattern) { return raw.find(pattern) != std::string::npos; };
        hdxf_test::assert_true(contains(std::string("\xE9\x03" "HDXF_TEST\0", 12)), what + ": 1001 as E9 03");
        hdxf_test::assert_true(contains(std::string("\x2E\x04\xFB\xFF", 4)), what + ": 1070 -5");
        hdxf_test::assert_true(contains(std::string("\x2F\x04\x70\x11\x01\x00", 6)), what + ": 1071 70000");
        hdxf_test::assert_true(contains(std::string("\xEC\x03\x03\x0A\x0B\x0C", 6)), what + ": 1004");
        hdxf_test::assert_true(contains(std::string("\x18\x01\x02\x00", 4)), what + ": 280 as int16");
        // Groups 280 and 281 of the style in the parsed groups
        bool dim_280 = false;
        for (const auto& r : records_of(groups)) {
            if (r.section == "TABLES" && r.type() == "DIMSTYLE" && r.value(2) == "D1") {
                dim_280 = r.value(280) == "2" && r.value(281) == "1";
            }
        }
        hdxf_test::assert_true(dim_280, what + ": DIMSTYLE 280/281");
    }
}

// ASCII and binary of the same drawing are the same groups (numbers by value)
TEST(write_r13_r14_ascii_and_binary_are_the_same_groups) {
    for (DxfVersion v : older_versions) {
        std::string what = name_of(v);
        hdxf::Drawing dwg(v);
        dwg.add_line({0.5, -2.25, 0}, {10, 5, 3});
        dwg.add_circle({1, 2, 0}, 4.5);
        dwg.add_text({1, 1, 0}, "Hej", 2.5);
        hdxf::Entity& paper = dwg.add_line({0, 0, 0}, {5, 5, 0});
        paper.props.paper_space = 1;
        std::vector<Group> a = groups_of(save(dwg, DxfFormat::Ascii, what));
        std::vector<Group> b = groups_of(save(dwg, DxfFormat::Binary, what));
        hdxf_test::assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()), what + ": group count");
        using hdxf::internal::GroupValueType;
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
            hdxf_test::assert_eq(a[i].code, b[i].code, what + ": code of group " + std::to_string(i));
            if (hdxf::internal::group_value_type(a[i].code) == GroupValueType::Double) {
                double x = std::atof(a[i].value.c_str());
                double y = std::atof(b[i].value.c_str());
                hdxf_test::assert_true(std::fabs(x - y) <= 1e-12 * std::max(1.0, std::fabs(x)),
                                       what + ": value of group " + std::to_string(i) + ": " + a[i].value + " / " + b[i].value);
            } else {
                hdxf_test::assert_eq(a[i].value, b[i].value, what + ": value of group " + std::to_string(i));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 7.5: the base structure (B89, B97)
// ---------------------------------------------------------------------------

TEST(write_r13_r14_empty_drawing_structure) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing dwg(v);
            std::vector<uint8_t> out = save(dwg, format, what);
            if (format == DxfFormat::Ascii) dxf_structure::check_structure(out, what);
            std::vector<dxf_structure::Rec> recs = records_of(groups_of(out));

            // The nine tables in AutoCAD's order, each with its handle and no owner (family N, D1)
            std::vector<std::string> tables;
            for (const auto* r : in_section(recs, "TABLES")) {
                if (r->type() == "TABLE") {
                    tables.push_back(r->value(2));
                    hdxf_test::assert_false(has_code(*r, 330), what + ": table owner");
                    hdxf_test::assert_false(r->value(5).empty(), what + ": table handle");
                }
            }
            hdxf_test::assert_true(tables == std::vector<std::string>({"VPORT", "LTYPE", "LAYER", "STYLE", "VIEW",
                                                                       "UCS", "APPID", "DIMSTYLE", "BLOCK_RECORD"}),
                                   what + ": tables");

            // The two space blocks, in capitals as AutoCAD's files have them, with their BLOCK_RECORDs
            // (no owner, no layout in 340)
            const dxf_structure::Rec* model_record = record_named(recs, "TABLES", "BLOCK_RECORD", "*MODEL_SPACE");
            const dxf_structure::Rec* paper_record = record_named(recs, "TABLES", "BLOCK_RECORD", "*PAPER_SPACE");
            hdxf_test::assert_true(model_record && paper_record, what + ": BLOCK_RECORDs");
            if (!model_record || !paper_record) return;
            for (const auto* r : {model_record, paper_record}) {
                hdxf_test::assert_false(has_code(*r, 340), what + ": BLOCK_RECORD without 340");
                hdxf_test::assert_false(has_code(*r, 330), what + ": BLOCK_RECORD without owner");
                hdxf_test::assert_eq(value_after(*r, "AcDbBlockTableRecord", 2), r->value(2), what);
            }
            std::vector<std::string> blocks;
            for (const auto* r : in_section(recs, "BLOCKS")) {
                if (r->type() != "BLOCK") continue;
                blocks.push_back(r->value(2));
                const dxf_structure::Rec* record = record_named(recs, "TABLES", "BLOCK_RECORD", r->value(2));
                hdxf_test::assert_true(record != nullptr, what + ": record of " + r->value(2));
                hdxf_test::assert_false(has_code(*r, 330), what + ": BLOCK without owner");
                hdxf_test::assert_eq(value_after(*r, "AcDbBlockBegin", 3), r->value(2), what + ": 3");
                bool paper = r->value(2) == "*PAPER_SPACE";
                hdxf_test::assert_eq(r->value(67), paper ? "1" : "", what + ": 67 of " + r->value(2));
            }
            hdxf_test::assert_true(blocks == std::vector<std::string>({"*MODEL_SPACE", "*PAPER_SPACE"}),
                                   what + ": blocks");
            for (const auto* r : in_section(recs, "BLOCKS")) {
                if (r->type() != "ENDBLK") continue;
                hdxf_test::assert_true(has_code(*r, 5) && !has_code(*r, 330), what + ": ENDBLK handle, no owner");
            }

            // OBJECTS: the root dictionary (owner 0) with ACAD_GROUP and ACAD_MLINESTYLE, the
            // STANDARD multiline style — no layout, plot setting or plot style anywhere
            std::vector<std::string> objects = types_in(recs, "OBJECTS");
            hdxf_test::assert_true(objects == std::vector<std::string>({"DICTIONARY", "DICTIONARY", "DICTIONARY", "MLINESTYLE"}),
                                   what + ": objects");
            std::vector<const dxf_structure::Rec*> rec_objects = in_section(recs, "OBJECTS");
            if (rec_objects.size() != 4) return;
            const dxf_structure::Rec* root = rec_objects[0];
            hdxf_test::assert_false(has_code(*root, 330), what + ": root without owner");
            std::vector<std::string> entries;
            for (const auto& g : root->groups) if (g.first == 3) entries.push_back(g.second);
            hdxf_test::assert_true(entries == std::vector<std::string>({"ACAD_GROUP", "ACAD_MLINESTYLE"}),
                                   what + ": root entries");
            // The ACAD_GROUP and ACAD_MLINESTYLE dictionaries are owned by the root and have it as reactor
            for (size_t i = 1; i < 3; ++i) {
                std::string text = groups_text(*rec_objects[i]);
                hdxf_test::assert_true(text.find("102={ACAD_REACTORS;330=" + root->value(5) + ";102=};") != std::string::npos,
                                       what + ": reactor of the dictionary: " + text);
            }
            const dxf_structure::Rec* style = rec_objects[3];
            hdxf_test::assert_eq(value_after(*style, "AcDbMlineStyle", 2), "STANDARD", what + ": STANDARD");
            hdxf_test::assert_eq(rec_objects[2]->value(350), style->value(5), what + ": ACAD_MLINESTYLE to STANDARD");

            // No XDATA TREAT_AS_HARD (family A2000's), no 330 outside a reactor group anywhere (D1)
            hdxf_test::assert_false(groups_text(*root).find("TREAT_AS_HARD") != std::string::npos, what + ": root XDATA");
            for (const auto& r : recs) {
                bool in_list = false;
                for (const auto& g : r.groups) {
                    if (g.first == 102) in_list = !g.second.empty() && g.second[0] == '{';
                    hdxf_test::assert_false(g.first == 330 && !in_list, what + ": 330 outside a reactor group in " + r.type());
                }
            }

            // Nothing of 2000's: no LAYOUT, plot settings, plot style, placeholder, and no name of them
            for (const auto& r : recs) {
                hdxf_test::assert_false(r.type() == "LAYOUT" || r.type() == "PLOTSETTINGS" ||
                                        r.type() == "ACDBPLACEHOLDER" || r.type() == "ACDBDICTIONARYWDFLT",
                                        what + ": " + r.type());
                for (const auto& g : r.groups) {
                    hdxf_test::assert_false(g.second == "AcDbLayout" || g.second == "AcDbPlotSettings" ||
                                            g.second == "ACAD_LAYOUT" || g.second == "ACAD_PLOTSTYLENAME" ||
                                            g.second == "ACAD_PLOTSETTINGS" || g.second == "Layout1",
                                            what + ": " + g.second);
                }
            }
            // CLASSES: nothing in the drawing needs a class
            hdxf_test::assert_eq(static_cast<int>(types_in(recs, "CLASSES").size()), 0, what + ": CLASS");
        }
    }
}

TEST(write_r13_r14_model_and_paper_space) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing dwg(v);
            dwg.add_line({0, 0, 0}, {10, 0, 0});
            dwg.add_circle({5, 5, 0}, 2);
            hdxf::Entity& on_paper = dwg.add_line({0, 0, 0}, {0, 10, 0});
            on_paper.props.paper_space = 1;
            std::vector<uint8_t> out = save(dwg, format, what);
            if (format == DxfFormat::Ascii) dxf_structure::check_structure(out, what);
            std::vector<dxf_structure::Rec> recs = records_of(groups_of(out));
            int in_model = 0, in_paper = 0;
            for (const auto* r : in_section(recs, "ENTITIES")) {
                hdxf_test::assert_true(has_code(*r, 5), what + ": entity handle");
                if (r->value(67) == "1") {
                    ++in_paper;
                    hdxf_test::assert_false(has_code(*r, 330), what + ": paper space entity without owner");
                } else {
                    ++in_model;
                    hdxf_test::assert_false(has_code(*r, 330), what + ": model space entity without owner");
                }
                hdxf_test::assert_eq(value_after(*r, "AcDbEntity", 8), "0", what + ": layer");
            }
            hdxf_test::assert_eq(in_model, 2, what + ": model space entities");
            hdxf_test::assert_eq(in_paper, 1, what + ": paper space entities");

            // Read back: the entities in their spaces
            hdxf::Drawing back;
            hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), what + ": " + back.last_error());
            hdxf_test::assert_true(back.version() == v, what + ": version");
            hdxf_test::assert_eq(static_cast<int>(back.entities().size()), 3, what + ": entities");
            int paper_space = 0;
            for (const auto& e : back.entities()) paper_space += e.props.paper_space != 0;
            hdxf_test::assert_eq(paper_space, 1, what + ": paper space entity read back");
        }
    }
}

// A block with an INSERT, a group and a multiline: the base structure carries them
TEST(write_r13_r14_blocks_group_and_mline) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing dwg(v);
            hdxf::Block& block = dwg.add_block("PART");
            block.entities.push_back(dwg.add_line({0, 0, 0}, {1, 0, 0}));
            dwg.entities().clear();
            dwg.add_insert("PART", {5, 5, 0});
            dwg.add_line({0, 0, 0}, {3, 0, 0});
            dwg.add_mline({{0, 0, 0}, {5, 0, 0}, {5, 5, 0}});
            std::vector<hdxf::Entity*> members = {&dwg.entities()[1]};
            std::string group = dwg.add_group("G", members).handle;
            hdxf_test::assert_false(group.empty(), what);
            std::vector<uint8_t> out = save(dwg, format, what);
            if (format == DxfFormat::Ascii) dxf_structure::check_structure(out, what);
            std::vector<dxf_structure::Rec> recs = records_of(groups_of(out));
            hdxf_test::assert_true(record_named(recs, "TABLES", "BLOCK_RECORD", "PART") != nullptr, what);
            std::vector<std::string> objects = types_in(recs, "OBJECTS");
            hdxf_test::assert_true(has(objects, "GROUP") && has(objects, "MLINESTYLE"), what + ": objects");
            hdxf_test::assert_false(has(objects, "LAYOUT"), what + ": LAYOUT");
        }
    }
}

// R13 -> R14 -> 2000 -> R13 on a new drawing: the same records, nothing left over, nothing twice
TEST(write_r13_r14_version_cycle_is_idempotent) {
    auto objects_of = [](const hdxf::Drawing& dwg) {
        std::vector<std::string> out;
        for (const auto& o : dwg.objects()) {
            std::string kind = std::to_string(static_cast<int>(o.type));
            if (o.type == hdxf::ObjectType::Dictionary) kind += ":" + std::to_string(o.as_dictionary().entries.size());
            out.push_back(kind);
        }
        std::sort(out.begin(), out.end());
        return out;
    };
    auto blocks_of = [](const hdxf::Drawing& dwg) {
        std::vector<std::string> out;
        for (const auto& b : dwg.blocks()) out.push_back(b.name);
        return out;
    };

    hdxf::Drawing fresh13(DxfVersion::R13);
    hdxf::Drawing fresh14(DxfVersion::R14);
    hdxf::Drawing fresh2000(DxfVersion::R2000);

    hdxf::Drawing dwg(DxfVersion::R13);
    dwg.add_line({0, 0, 0}, {1, 1, 0});
    hdxf_test::assert_true(objects_of(dwg) == objects_of(fresh13), "R13 objects");
    for (int round = 0; round < 3; ++round) {
        std::string what = "round " + std::to_string(round);
        dwg.set_version(DxfVersion::R14);
        hdxf_test::assert_true(objects_of(dwg) == objects_of(fresh14), what + ": R14 objects");
        hdxf_test::assert_true(blocks_of(dwg) == blocks_of(fresh14), what + ": R14 blocks");
        dwg.set_version(DxfVersion::R2000);
        hdxf_test::assert_true(objects_of(dwg) == objects_of(fresh2000), what + ": 2000 objects");
        hdxf_test::assert_true(blocks_of(dwg) == blocks_of(fresh2000), what + ": 2000 blocks");
        hdxf_test::assert_eq(static_cast<int>(dwg.layouts().size()), 3, what + ": layouts");
        dxf_structure::check_structure(save(dwg, DxfFormat::Ascii, what + " 2000"), what + " 2000");
        dwg.set_version(DxfVersion::R13);
        hdxf_test::assert_true(objects_of(dwg) == objects_of(fresh13), what + ": R13 objects");
        hdxf_test::assert_true(blocks_of(dwg) == blocks_of(fresh13), what + ": R13 blocks");
        hdxf_test::assert_eq(static_cast<int>(dwg.layouts().size()), 0, what + ": no layouts");
        // The same version again: nothing more
        dwg.set_version(DxfVersion::R13);
        hdxf_test::assert_true(objects_of(dwg) == objects_of(fresh13), what + ": R13 twice");
        dxf_structure::check_structure(save(dwg, DxfFormat::Ascii, what + " R13"), what + " R13");
        hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), 1, what + ": entities");
    }

    // R13 set to R14 is written as a drawing made as R14 is
    hdxf::Drawing a(DxfVersion::R13);
    a.set_version(DxfVersion::R14);
    std::vector<Group> ga = groups_of(save(a, DxfFormat::Ascii, "R13 to R14"));
    std::vector<Group> gb = groups_of(save(fresh14, DxfFormat::Ascii, "R14"));
    hdxf_test::assert_eq(static_cast<int>(ga.size()), static_cast<int>(gb.size()), "R13 to R14: groups");
    for (size_t i = 0; i < ga.size() && i < gb.size(); ++i) {
        hdxf_test::assert_true(ga[i].code == gb[i].code && ga[i].value == gb[i].value,
                               "R13 to R14: group " + std::to_string(i));
    }

    // 2000 first: R13 loses 2000's layouts and gets nothing it did not have
    hdxf::Drawing from2000(DxfVersion::R2000);
    from2000.set_version(DxfVersion::R13);
    hdxf_test::assert_true(objects_of(from2000) == objects_of(fresh13), "2000 to R13 objects");
    hdxf_test::assert_true(blocks_of(from2000) == blocks_of(fresh13), "2000 to R13 blocks");
    for (const auto& b : from2000.blocks()) {
        hdxf_test::assert_true(b.record.layout_handle.empty(), "layout pointer of " + b.name);
    }
    from2000.set_version(DxfVersion::R2000);
    hdxf_test::assert_true(objects_of(from2000) == objects_of(fresh2000), "back to 2000");
    from2000.set_version(DxfVersion::R2007);
    from2000.set_version(DxfVersion::R14);
    std::vector<uint8_t> out14 = save(from2000, DxfFormat::Ascii, "2007 to R14");
    dxf_structure::check_structure(out14, "2007 to R14");
    for (const auto& r : records_of(groups_of(out14))) {
        hdxf_test::assert_false(r.type() == "LAYOUT" || r.type() == "PLOTSETTINGS", "2000's object in R14");
    }
}

// What the program made stays through the cycle
TEST(write_r13_r14_version_cycle_keeps_the_programs_objects) {
    hdxf::Drawing dwg(DxfVersion::R2000);
    dwg.add_line({0, 0, 0}, {1, 0, 0});
    dwg.add_line({0, 0, 0}, {0, 1, 0});
    std::vector<hdxf::Entity*> members = {&dwg.entities()[0]};
    dwg.add_group("KEEP", members);
    dwg.set_version(DxfVersion::R13);
    dwg.set_version(DxfVersion::R2000);
    dwg.set_version(DxfVersion::R14);
    int groups = 0;
    for (const auto& o : dwg.objects()) groups += o.type == hdxf::ObjectType::Group;
    hdxf_test::assert_eq(groups, 1, "the group");
    dxf_structure::check_structure(save(dwg, DxfFormat::Ascii, "R14"), "R14 with a group");
}

// ---------------------------------------------------------------------------
// Layouts and the types R13/R14 do not have (B89, B94): exact errors, nothing changed
// ---------------------------------------------------------------------------

// A default layout the program changed is the program's: set_version() keeps it, and the save is
// refused, instead of the change being thrown away (Codex review, phase 7)
TEST(write_r13_r14_changed_default_layout_is_kept) {
    for (DxfVersion v : older_versions) {
        std::string what = name_of(v);
        hdxf::Drawing dwg(DxfVersion::R2000);
        hdxf::Object* layout1 = nullptr;
        for (hdxf::Object* o : dwg.layouts()) {
            if (o->as_layout().name == "Layout1") layout1 = o;
        }
        hdxf_test::assert_true(layout1 != nullptr, what + ": Layout1");
        layout1->as_layout().plot_settings.margin_left = 5.0;
        dwg.set_version(v);
        hdxf_test::assert_true(dwg.save_to_memory(DxfFormat::Ascii).empty(), what + ": saved");
        hdxf_test::assert_eq(dwg.last_error(), "LAYOUT requires DXF 2000 or later", what);
        dwg.set_version(DxfVersion::R2000);
        bool kept = false;
        for (const hdxf::Object* o : dwg.layouts()) {
            if (o->as_layout().name == "Layout1" && o->as_layout().plot_settings.margin_left == 5.0) kept = true;
        }
        hdxf_test::assert_true(kept, what + ": the change is kept");
    }
}

TEST(write_r13_r14_layout_needs_2000) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing dwg(v);
            dwg.add_line({0, 0, 0}, {1, 0, 0});
            dwg.add_layout("Plan");
            std::filesystem::path path = temp_file("hdxf_r13_r14_layout.dxf");
            {
                std::ofstream f(path, std::ios::binary);
                f << "before";
            }
            std::string handseed = handseed_of(dwg);
            size_t objects = dwg.objects().size();
            hdxf_test::assert_true(dwg.save_to_memory(format).empty(), what + ": saved");
            hdxf_test::assert_eq(dwg.last_error(), "LAYOUT requires DXF 2000 or later", what);
            hdxf_test::assert_false(dwg.save_as(path, format), what + ": save_as");
            hdxf_test::assert_eq(dwg.last_error(), "LAYOUT requires DXF 2000 or later", what);
            hdxf_test::assert_eq(read_all(path), "before", what + ": file unchanged");
            hdxf_test::assert_eq(handseed_of(dwg), handseed, what + ": $HANDSEED");
            hdxf_test::assert_eq(static_cast<int>(dwg.objects().size()), static_cast<int>(objects), what + ": objects");
            for (const auto& e : dwg.entities()) hdxf_test::assert_true(e.props.handle.empty(), what + ": handle kept");
            std::filesystem::remove(path);

            // Another version of the same drawing (still R13/R14 after set_version): the layout stays
            dwg.set_version(v == DxfVersion::R13 ? DxfVersion::R14 : DxfVersion::R13);
            hdxf_test::assert_eq(static_cast<int>(dwg.layouts().size()), 1, what + ": the program's layout stays");
            hdxf_test::assert_true(dwg.save_to_memory().empty(), what + ": still refused");

            // As 2000 it is written, with the layouts the version cycle gives it: Plan (on
            // *Paper_Space0), Model and Layout1
            dwg.set_version(DxfVersion::R2000);
            dxf_structure::check_structure(save(dwg, DxfFormat::Ascii, what + " as 2000"), what + " as 2000");
            hdxf_test::assert_eq(static_cast<int>(dwg.layouts().size()), 3, what + ": layouts as 2000");
        }
    }
}

TEST(write_r13_r14_types_of_later_versions) {
    struct Case {
        const char* what;
        std::function<void(hdxf::Drawing&)> add;
        DxfVersion first;           // the first version that has it
        const char* error;          // for the older ones
    };
    const Case cases[] = {
        {"LWPOLYLINE", [](hdxf::Drawing& d) { d.add_lwpolyline({{0, 0}, {1, 0}, {1, 1}}); }, DxfVersion::R14,
         "LWPOLYLINE requires DXF R14 or later"},
        {"WIPEOUT", [](hdxf::Drawing& d) { d.add_wipeout({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}}); }, DxfVersion::R2000,
         "WIPEOUT requires DXF 2000 or later"},
        {"MESH", [](hdxf::Drawing& d) { d.add_mesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {{0, 1, 2}}, 0); },
         DxfVersion::R2000, "MESH requires DXF 2000 or later"},
    };
    for (const auto& c : cases) {
        for (DxfVersion v : older_versions) {
            for (DxfFormat format : both_formats) {
                std::string what = std::string(c.what) + " as " + name_of(v) + " " + name_of(format);
                hdxf::Drawing dwg(v);
                dwg.add_line({0, 0, 0}, {1, 0, 0});
                c.add(dwg);
                std::string handseed = handseed_of(dwg);
                std::filesystem::path path = temp_file("hdxf_r13_r14_type.dxf");
                std::filesystem::remove(path);
                if (v >= c.first) {
                    dxf_structure::check_structure(save(dwg, DxfFormat::Ascii, what), what);
                    save(dwg, format, what);
                } else {
                    hdxf_test::assert_true(dwg.save_to_memory(format).empty(), what + ": saved");
                    hdxf_test::assert_eq(dwg.last_error(), c.error, what);
                    hdxf_test::assert_false(dwg.save_as(path, format), what + ": save_as");
                    hdxf_test::assert_false(std::filesystem::exists(path), what + ": file created");
                    hdxf_test::assert_eq(handseed_of(dwg), handseed, what + ": $HANDSEED");
                    // (the first, a LINE, got no handle; a WIPEOUT has one from the call that made it)
                    hdxf_test::assert_true(dwg.entities().at(0).props.handle.empty(), what + ": handle kept");
                }
            }
        }
    }
}

// R13's HATCH and IMAGE: refused (D8, D9: R13 has neither, steps 7.14 and 7.15); R14 has them
TEST(write_r13_r14_hatch_and_image_in_r13) {
    hdxf::Drawing r13(DxfVersion::R13);
    r13.add_hatch({{0, 0}, {1, 0}, {1, 1}});
    hdxf_test::assert_true(r13.save_to_memory().empty(), "HATCH as R13");
    hdxf_test::assert_eq(r13.last_error(), "HATCH requires DXF R14 or later");
    hdxf::Drawing r13_image(DxfVersion::R13);
    r13_image.add_image("picture.png", {0, 0, 0}, {10, 10}, 5);
    hdxf_test::assert_true(r13_image.save_to_memory().empty(), "IMAGE as R13");
    hdxf_test::assert_eq(r13_image.last_error(), "IMAGE requires DXF R14 or later");

    hdxf::Drawing r14(DxfVersion::R14);
    r14.add_hatch({{0, 0}, {1, 0}, {1, 1}});
    hdxf_test::assert_false(r14.save_to_memory().empty(), r14.last_error());
}

// ---------------------------------------------------------------------------
// A drawing read from R13/R14 is written as that version only (B86); the full round trip is 7.18
// ---------------------------------------------------------------------------

TEST(write_r13_r14_read_file_only_as_its_version) {
    struct File {
        const char* name;
        DxfVersion version;
    };
    const File files[] = {{"small_r13.dxf", DxfVersion::R13}, {"bin_dxf_r13.dxf", DxfVersion::R13},
                          {"small_r14.dxf", DxfVersion::R14}, {"bin_dxf_r14.dxf", DxfVersion::R14}};
    for (const auto& f : files) {
        std::filesystem::path path = external(f.name);
        if (!std::filesystem::exists(path)) {
            hdxf_test::skip(std::string("external test file missing: ezdxf/") + f.name);
            return;
        }
        std::string what = f.name;
        hdxf::Drawing dwg;
        hdxf_test::assert_true(dwg.load(path), what + ": " + dwg.last_error());
        hdxf_test::assert_true(dwg.version() == f.version, what + ": version");
        const size_t entities = dwg.entities().size();
        for (DxfFormat format : both_formats) {
            std::vector<uint8_t> out = dwg.save_to_memory(format);
            hdxf_test::assert_false(out.empty(), what + " " + name_of(format) + ": " + dwg.last_error());
            std::vector<Group> groups = groups_of(out);
            hdxf_test::assert_eq(header_value(groups, "$ACADVER", 1), std::string(hdxf::acadver_string(f.version)), what);
            // What was written reads back as the same version
            hdxf::Drawing back;
            hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), what + ": " + back.last_error());
            hdxf_test::assert_true(back.version() == f.version, what + ": version read back");
            hdxf_test::assert_eq(static_cast<int>(back.entities().size()), static_cast<int>(entities), what + ": entities");
        }
        // Another version is a conversion (phase 8): refused, nothing changed
        for (DxfVersion other : {DxfVersion::R12, DxfVersion::R13, DxfVersion::R14, DxfVersion::R2000, DxfVersion::R2018}) {
            if (other == f.version) continue;
            dwg.set_version(other);
            hdxf_test::assert_true(dwg.save_to_memory().empty(), what + ": saved as another version");
            std::string error = dwg.last_error();
            hdxf_test::assert_true(error.rfind(std::string("Writing DXF ") + name_of(f.version) + " as ", 0) == 0 &&
                                   error.find("is not supported yet (the drawing was read from that version)") != std::string::npos,
                                   what + ": " + error);
        }
        dwg.set_version(f.version);
        hdxf_test::assert_false(dwg.save_to_memory().empty(), what + ": own version again: " + dwg.last_error());
        hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), static_cast<int>(entities), what + ": entities unchanged");
    }
}

// ---------------------------------------------------------------------------
// D1: the two families of AutoCAD file. A new drawing is family N (no 330 owner outside a reactor
// group); a drawing read from a file is written in the file's family, and a record written anew in
// it follows (330 if and only if the file's records have it)
// ---------------------------------------------------------------------------

namespace {

std::filesystem::path external_in(const std::string& dir, const std::string& name) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / dir / name;
}

// Whether a record has an owner 330 outside the {...} reactor and dictionary groups
bool has_owner_group(const dxf_structure::Rec& r) {
    bool in_list = false;
    for (const auto& g : r.groups) {
        if (g.first == 102) in_list = !g.second.empty() && g.second[0] == '{';
        else if (g.first == 330 && !in_list) return true;
    }
    return false;
}

} // namespace

TEST(write_r13_r14_family_follows_the_file_read) {
    struct Case {
        const char* dir;
        const char* file;
        DxfVersion version;
        bool owners;          // family A2000 (and the ODA-type files): owner groups
    };
    // small_r13 is AutoCAD 2004 saving as R13 (family A2000), bin_dxf_* are ODA's (330 too);
    // r13_v is AutoCAD R13/R14 (family N), HEAD.DXF AutoCAD R14
    const Case cases[] = {{"ezdxf", "small_r13.dxf", DxfVersion::R13, true},
                          {"ezdxf", "bin_dxf_r13.dxf", DxfVersion::R13, true},
                          {"ezdxf", "bin_dxf_r14.dxf", DxfVersion::R14, true},
                          {"libre", "r13_v.dxf", DxfVersion::R13, false},
                          {"spatialauxetic", "HEAD.DXF", DxfVersion::R14, false}};
    for (const Case& c : cases) {
        std::filesystem::path path = external_in(c.dir, c.file);
        if (!std::filesystem::exists(path)) {
            hdxf_test::skip(std::string("external test file missing: ") + c.dir + "/" + c.file);
            return;
        }
        for (DxfFormat format : both_formats) {
            std::string what = std::string(c.file) + " " + name_of(format);
            hdxf::Drawing dwg;
            hdxf_test::assert_true(dwg.load(path), what + ": " + dwg.last_error());
            hdxf_test::assert_true(dwg.version() == c.version, what + ": version");
            // A record of each kind written anew: an entity, a table record
            dwg.add_layer("HDXF_FAMILY");
            dwg.add_line({0, 0, 0}, {3, 4, 0}, "HDXF_FAMILY");
            std::vector<uint8_t> out = save(dwg, format, what);
            std::vector<dxf_structure::Rec> recs = records_of(groups_of(out));
            const dxf_structure::Rec* layer = record_named(recs, "TABLES", "LAYER", "HDXF_FAMILY");
            hdxf_test::assert_true(layer != nullptr, what + ": the new layer");
            if (layer) hdxf_test::assert_eq(has_owner_group(*layer), c.owners, what + ": owner of the new layer");
            const dxf_structure::Rec* line = nullptr;
            for (const auto* r : in_section(recs, "ENTITIES")) {
                if (r->type() == "LINE" && value_after(*r, "AcDbEntity", 8) == "HDXF_FAMILY") line = r;
            }
            hdxf_test::assert_true(line != nullptr, what + ": the new line");
            if (line) hdxf_test::assert_eq(has_owner_group(*line), c.owners, what + ": owner of the new line");
        }
    }
}

// The same file read and written without a change keeps its records: a family N file stays without
// owner groups, a family A2000 file keeps them
TEST(write_r13_r14_family_of_a_file_is_kept_unchanged) {
    struct Case { const char* dir; const char* file; bool owners; };
    const Case cases[] = {{"ezdxf", "small_r13.dxf", true}, {"libre", "r13_v.dxf", false},
                          {"spatialauxetic", "HEAD.DXF", false}};
    for (const Case& c : cases) {
        std::filesystem::path path = external_in(c.dir, c.file);
        if (!std::filesystem::exists(path)) {
            hdxf_test::skip(std::string("external test file missing: ") + c.dir + "/" + c.file);
            return;
        }
        hdxf::Drawing dwg;
        hdxf_test::assert_true(dwg.load(path), std::string(c.file) + ": " + dwg.last_error());
        std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii, c.file);
        std::vector<dxf_structure::Rec> recs = records_of(groups_of(out));
        bool any_owner = false;
        for (const auto* r : in_section(recs, "TABLES")) {
            if (r->type() == "TABLE" || r->type() == "LAYER" || r->type() == "LTYPE") {
                any_owner = any_owner || has_owner_group(*r);
            }
        }
        hdxf_test::assert_eq(any_owner, c.owners, std::string(c.file) + ": owner groups of the tables");
    }
}

// A drawing copied from a read one keeps the family; a second load resets it; a new drawing has none
TEST(write_r13_r14_family_is_copied_with_the_drawing) {
    std::filesystem::path path = external_in("ezdxf", "small_r13.dxf");
    std::filesystem::path n_path = external_in("libre", "r13_v.dxf");
    if (!std::filesystem::exists(path) || !std::filesystem::exists(n_path)) {
        hdxf_test::skip("small_r13.dxf or r13_v.dxf is missing");
        return;
    }
    hdxf::Drawing read;
    hdxf_test::assert_true(read.load(path), read.last_error());
    hdxf::Drawing copy = read;
    copy.add_layer("HDXF_COPY");
    std::vector<dxf_structure::Rec> recs = records_of(groups_of(save(copy, DxfFormat::Ascii, "copy")));
    const dxf_structure::Rec* layer = record_named(recs, "TABLES", "LAYER", "HDXF_COPY");
    hdxf_test::assert_true(layer && has_owner_group(*layer), "the copy of an A2000 file has owner groups");

    hdxf_test::assert_true(read.load(n_path), read.last_error());
    read.add_layer("HDXF_AGAIN");
    recs = records_of(groups_of(save(read, DxfFormat::Ascii, "second load")));
    layer = record_named(recs, "TABLES", "LAYER", "HDXF_AGAIN");
    hdxf_test::assert_true(layer && !has_owner_group(*layer), "after loading a family N file");

    hdxf::Drawing fresh;
    fresh.set_version(DxfVersion::R14);
    fresh.add_layer("HDXF_NEW");
    recs = records_of(groups_of(save(fresh, DxfFormat::Ascii, "new")));
    layer = record_named(recs, "TABLES", "LAYER", "HDXF_NEW");
    hdxf_test::assert_true(layer && !has_owner_group(*layer), "a new drawing is family N");
}

// ---------------------------------------------------------------------------
// The header of a new drawing against AutoCAD's own files (format profile 3.2: libre/r13_v.dxf for R13,
// spatialauxetic/HEAD.DXF for R14): the same variables, in the same order, with the same codes
// ---------------------------------------------------------------------------

TEST(write_r13_r14_header_equals_autocads_files_in_names_codes_and_order) {
    struct Case { const char* dir; const char* file; DxfVersion version; size_t count; };
    const Case cases[] = {{"libre", "r13_v.dxf", DxfVersion::R13, 162},
                          {"spatialauxetic", "HEAD.DXF", DxfVersion::R14, 164}};
    for (const Case& c : cases) {
        std::filesystem::path path = external_in(c.dir, c.file);
        if (!std::filesystem::exists(path)) {
            hdxf_test::skip(std::string("external test file missing: ") + c.dir + "/" + c.file);
            return;
        }
        std::ifstream in(path, std::ios::binary);
        std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto theirs = header_of(groups_of(file));
        hdxf::Drawing dwg(c.version);
        for (DxfFormat format : both_formats) {
            std::string what = std::string(c.file) + " " + name_of(format);
            auto ours = header_of(groups_of(save(dwg, format, what)));
            hdxf_test::assert_eq(static_cast<int>(theirs.size()), static_cast<int>(c.count), what + ": the file");
            hdxf_test::assert_eq(static_cast<int>(ours.size()), static_cast<int>(c.count), what + ": count");
            for (size_t i = 0; i < theirs.size() && i < ours.size(); ++i) {
                hdxf_test::assert_eq(ours[i].first, theirs[i].first, what + ": variable " + std::to_string(i));
                std::vector<int> our_codes, their_codes;
                for (const auto& g : ours[i].second) our_codes.push_back(g.code);
                for (const auto& g : theirs[i].second) their_codes.push_back(g.code);
                hdxf_test::assert_true(our_codes == their_codes, what + ": codes of " + theirs[i].first);
            }
        }
    }
}

// $ACADMAINTVER: none in R13, 0 in a new R14 drawing (D3), the file's value in a drawing read from R14
TEST(write_r13_r14_maintenance_version) {
    hdxf::Drawing r13(DxfVersion::R13);
    hdxf_test::assert_eq(header_value(groups_of(save(r13, DxfFormat::Ascii, "R13")), "$ACADMAINTVER", 70), "<none>");
    hdxf::Drawing r14(DxfVersion::R14);
    hdxf_test::assert_eq(header_value(groups_of(save(r14, DxfFormat::Ascii, "R14")), "$ACADMAINTVER", 70), "0");
    std::filesystem::path path = external_in("spatialauxetic", "HEAD.DXF");     // AutoCAD R14, maintenance 8
    if (!std::filesystem::exists(path)) {
        hdxf_test::skip("spatialauxetic/HEAD.DXF is missing");
        return;
    }
    hdxf::Drawing read;
    hdxf_test::assert_true(read.load(path), read.last_error());
    hdxf_test::assert_eq(header_value(groups_of(save(read, DxfFormat::Ascii, "HEAD")), "$ACADMAINTVER", 70), "8");
}

// ---------------------------------------------------------------------------
// Family N and the reader: an object without an owner group is owned by the reactor that holds it
// (a dictionary with the object as an entry, an extension dictionary's owner), the root has "0";
// the family itself is told from the file's table heads and BLOCK records (D1)
// ---------------------------------------------------------------------------

namespace {

std::vector<uint8_t> file_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Whether the file's table heads or BLOCK records have an owner group
bool file_has_owner_groups(const std::vector<uint8_t>& bytes) {
    std::vector<dxf_structure::Rec> recs = records_of(groups_of(bytes));
    for (const auto& r : recs) {
        if ((r.section == "TABLES" && r.type() == "TABLE") || (r.section == "BLOCKS" && r.type() == "BLOCK")) {
            if (has_owner_group(r)) return true;
        }
    }
    return false;
}

} // namespace

TEST(write_r13_r14_read_objects_find_their_owner_in_both_families) {
    struct Case { const char* dir; const char* file; bool owners; };
    const Case cases[] = {{"ezdxf", "small_r13.dxf", true},
                          {"ezdxf", "small_r14.dxf", true},
                          {"ezdxf", "bin_dxf_r13.dxf", true},
                          {"ezdxf", "bin_dxf_r14.dxf", true},
                          {"libre", "r13_v.dxf", false},
                          {"spatialauxetic", "HEAD.DXF", false},
                          {"gvsig", "dxfprueba.dxf", false},
                          {"local-r13r14", "BL15-1_davidsmarx_r14_dimension_hatch.dxf", false},
                          {"local-r13r14", "puppenhaus_4mm_r14_viewport_view.dxf", false},
                          {"local-r13r14", "Zurn_59251_r14_acad2000family.dxf", true},
                          {"local-r13r14", "altera_yeager_io_r14_imagedef.dxf", false},
                          {"local-r13r14", "altera_piranha_io_group_r13_imagedef.dxf", false},
                          {"local-r13r14", "Test3_Flash_Tank_I_r14.dxf", false}};
    int checked = 0;
    for (const Case& c : cases) {
        std::filesystem::path path = external_in(c.dir, c.file);
        if (!std::filesystem::exists(path)) continue;
        ++checked;
        const std::string what = std::string(c.file);
        std::vector<uint8_t> bytes = file_bytes(path);
        hdxf_test::assert_eq(file_has_owner_groups(bytes), c.owners, what + ": the file's family");

        hdxf::Drawing dwg;
        hdxf_test::assert_true(dwg.load(path), what + ": " + dwg.last_error());
        std::map<std::string, const hdxf::Object*> by_handle;
        for (const auto& o : dwg.objects()) by_handle[o.handle] = &o;
        int roots = 0;
        for (const auto& o : dwg.objects()) {
            if (o.owner == "0") {
                // only the root dictionary, and objects with neither an owner nor a reactor in the file
                if (o.type == hdxf::ObjectType::Dictionary && o.reactors.empty()) ++roots;
                continue;
            }
            hdxf_test::assert_false(o.owner.empty(), what + ": object " + o.handle + " has no owner");
            if (c.owners) continue;
            // family N: the owner is one of the reactors, and a dictionary that owns the object holds it
            hdxf_test::assert_true(std::find(o.reactors.begin(), o.reactors.end(), o.owner) != o.reactors.end(),
                                   what + ": owner of " + o.handle + " is a reactor");
            auto holder = by_handle.find(o.owner);
            if (holder != by_handle.end() && holder->second->type == hdxf::ObjectType::Dictionary) {
                bool holds = holder->second->xdictionary == o.handle;
                for (const auto& e : holder->second->as_dictionary().entries) holds = holds || e.handle == o.handle;
                // (a reactor dictionary that does not hold the object is a file's own link, kept as read)
                if (!holds) {
                    bool any = false;
                    for (const auto& r : o.reactors) {
                        auto h = by_handle.find(r);
                        if (h == by_handle.end() || h->second->type != hdxf::ObjectType::Dictionary) continue;
                        for (const auto& e : h->second->as_dictionary().entries) any = any || e.handle == o.handle;
                    }
                    hdxf_test::assert_false(any, what + ": " + o.handle + " has a reactor dictionary that holds it, not chosen as owner");
                }
            }
        }
        hdxf_test::assert_true(roots <= 1, what + ": at most one root dictionary");
    }
    if (checked == 0) hdxf_test::skip("no R13/R14 test file");
}

// Family A2000 (a read file with owner groups): what is written anew has the right owner: the table for a
// table record, the space's BLOCK_RECORD for an entity, the owning dictionary for an object
TEST(write_r13_r14_a2000_file_new_records_have_the_right_owner) {
    for (const char* name : {"small_r13.dxf", "bin_dxf_r14.dxf"}) {
        std::filesystem::path path = external_in("ezdxf", name);
        if (!std::filesystem::exists(path)) {
            hdxf_test::skip(std::string("external test file missing: ezdxf/") + name);
            return;
        }
        hdxf::Drawing dwg;
        hdxf_test::assert_true(dwg.load(path), std::string(name) + ": " + dwg.last_error());
        dwg.add_layer("HDXF_OWNER");
        dwg.add_line({0, 0, 0}, {3, 4, 0}, "HDXF_OWNER");
        std::vector<dxf_structure::Rec> recs = records_of(groups_of(save(dwg, DxfFormat::Ascii, name)));
        std::string layer_table;
        for (const auto* r : in_section(recs, "TABLES")) {
            if (r->type() == "TABLE" && r->value(2) == "LAYER") layer_table = r->value(5);
        }
        const dxf_structure::Rec* layer = record_named(recs, "TABLES", "LAYER", "HDXF_OWNER");
        hdxf_test::assert_true(layer != nullptr, std::string(name) + ": the new layer");
        if (layer) hdxf_test::assert_eq(layer->value(330), layer_table, std::string(name) + ": the layer's owner is the LAYER table");
        std::string model;
        for (const auto* r : in_section(recs, "TABLES")) {
            if (r->type() == "BLOCK_RECORD" && r->value(2).size() > 7 && r->value(2).substr(0, 7) == "*MODEL_") model = r->value(5);
            if (r->type() == "BLOCK_RECORD" && model.empty() && r->value(2) == "*Model_Space") model = r->value(5);
        }
        const dxf_structure::Rec* line = nullptr;
        for (const auto* r : in_section(recs, "ENTITIES")) {
            if (r->type() == "LINE" && r->value(8) == "HDXF_OWNER") line = r;
        }
        hdxf_test::assert_true(line != nullptr, std::string(name) + ": the new line");
        if (line) hdxf_test::assert_eq(line->value(330), model, std::string(name) + ": the line's owner is the model space record");
    }
}
