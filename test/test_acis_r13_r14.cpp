#include "test_framework.hpp"
#include "hdxf.hpp"
#include "acis.hpp"
#include "group_io.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// ACIS (3DSOLID, REGION, BODY) in DXF R13 and R14 (plan phase 7, step 7.17, B95).
//
// What is established: AutoCAD's R14 file (AC1014) has a REGION with the modeler group 70 = 1 and
// SAT 106 (ACIS 1.6: no product line, "-n" sequence numbers, no End-of-ACIS-data), in 1 lines of at
// most 194 characters; AutoCAD 2000 has 70 = 1 and SAT 400. So 70 is the modeler format, not the SAT
// version. The newest SAT of a target is the one its ACIS reads (R13 and R14: ACIS 1.6 =
// 106); a newer SAT, SAB (ACDSDATA, 2013+) or non-SAT data is an error, never converted and never
// given another header number. The tests that write a whole R13/R14 drawing (acis_r13_r14_write_*)
// go through the R13/R14 write path of steps 7.2–7.5. No golden files: the streams are compared
// with the SAT they were made from.
// ---------------------------------------------------------------------------

namespace {

using hdxf::AcisData;
using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::EntityType;

// A SAT 1.6 body (the plane face of SAT 106 in test_acis.cpp), `version` its header's first number
std::string small_sat(int version) {
    return std::to_string(version) + " 10 1 0\n"
           "-0 body $-1 $1 $-1 $-1 #\n"
           "-1 lump $-1 $-1 $2 $0 #\n"
           "-2 shell $-1 $-1 $-1 $3 $1 #\n"
           "-3 face $-1 $-1 $4 $2 $-1 $5 reversed double out #\n"
           "-4 loop $-1 $-1 $6 $3 #\n"
           "-5 plane-surface $-1 0 0 2 0 0 1 1 0 0 0 I I I I #\n"
           "-6 coedge $-1 $6 $6 $-1 $7 1 $4 $-1 #\n"
           "-7 edge $-1 $8 $8 $6 $9 forward #\n"
           "-8 vertex $-1 $7 $10 #\n"
           "-9 ellipse-curve $-1 3 4 2 0 0 1 2 0 0 0.5 I I #\n"
           "-10 point $-1 5 4 2 #\n";
}

// AutoCAD's enciphering: every character but the space and the control characters as 159 - c
std::string encipher(const std::string& line) {
    std::string out;
    for (char c : line) {
        unsigned char u = static_cast<unsigned char>(c);
        out += (u > 32 && u < 159) ? static_cast<char>(159 - u) : c;
    }
    return out;
}

// The 1/3 lines of a SAT text, a line per SAT line
std::vector<std::string> sat_lines(const std::string& sat) {
    std::vector<std::string> lines;
    std::string line;
    for (char c : sat) {
        if (c == '\n') {
            lines.push_back(encipher(line));
            line.clear();
        } else {
            line += c;
        }
    }
    if (!line.empty()) lines.push_back(encipher(line));
    return lines;
}

AcisData data_of(const std::string& sat) {
    AcisData d;
    d.lines = sat_lines(sat);
    return d;
}

// One body of a DXF file made for the reader
struct FileBody {
    std::string type;                   // 3DSOLID, REGION, BODY
    std::string handle;
    std::vector<std::string> lines;     // enciphered, as in the file
    int modeler = 1;                    // group 70
};

using Groups = std::vector<std::pair<int, std::string>>;

// The groups of a body as AutoCAD writes it: a line of 255 characters or more as 3s of 255 and the
// rest, perhaps empty, in 1 (the form of AutoCAD's 2000 files; AutoCAD's R14 file has no such line)
void body_groups(const FileBody& b, Groups& g) {
    g.push_back({0, b.type});
    g.push_back({5, b.handle});
    g.push_back({100, "AcDbEntity"});
    g.push_back({8, "0"});
    g.push_back({100, "AcDbModelerGeometry"});
    g.push_back({70, std::to_string(b.modeler)});
    for (const auto& line : b.lines) {
        size_t pos = 0;
        while (line.size() - pos >= 255) {
            g.push_back({3, line.substr(pos, 255)});
            pos += 255;
        }
        g.push_back({1, line.substr(pos)});
    }
}

// An R14 file (AC1014) with the bodies in ENTITIES and `in_block` in the block "SOLIDS"
std::vector<uint8_t> r14_file(const std::vector<FileBody>& bodies, DxfFormat format,
                              const std::vector<FileBody>& in_block = {}) {
    Groups g;
    g.push_back({0, "SECTION"});
    g.push_back({2, "HEADER"});
    g.push_back({9, "$ACADVER"});
    g.push_back({1, "AC1014"});
    g.push_back({9, "$HANDSEED"});
    g.push_back({5, "FFFF"});
    g.push_back({0, "ENDSEC"});
    if (!in_block.empty()) {
        g.push_back({0, "SECTION"});
        g.push_back({2, "BLOCKS"});
        g.push_back({0, "BLOCK"});
        g.push_back({5, "B0"});
        g.push_back({100, "AcDbEntity"});
        g.push_back({8, "0"});
        g.push_back({100, "AcDbBlockBegin"});
        g.push_back({2, "SOLIDS"});
        g.push_back({70, "0"});
        g.push_back({10, "0.0"});
        g.push_back({20, "0.0"});
        g.push_back({30, "0.0"});
        g.push_back({3, "SOLIDS"});
        g.push_back({1, ""});
        for (const auto& b : in_block) body_groups(b, g);
        g.push_back({0, "ENDBLK"});
        g.push_back({5, "B1"});
        g.push_back({100, "AcDbEntity"});
        g.push_back({8, "0"});
        g.push_back({100, "AcDbBlockEnd"});
        g.push_back({0, "ENDSEC"});
    }
    g.push_back({0, "SECTION"});
    g.push_back({2, "ENTITIES"});
    for (const auto& b : bodies) body_groups(b, g);
    g.push_back({0, "ENDSEC"});
    g.push_back({0, "EOF"});

    std::ostringstream out(std::ios::out | std::ios::binary);
    hdxf::internal::GroupWriter w(out, format, hdxf::internal::BinaryCodeWidth::TwoBytes);
    if (format == DxfFormat::Binary) w.write_sentinel();
    for (const auto& [code, value] : g) w.write_typed(code, value);
    std::string s = out.str();
    return std::vector<uint8_t>(s.begin(), s.end());
}

Drawing read_r14(const std::vector<uint8_t>& file) {
    Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(file.data(), file.size()), dwg.last_error());
    hdxf_test::assert_true(dwg.version() == DxfVersion::R14, "not read as R14");
    return dwg;
}

const hdxf::Entity& body_with_handle(const Drawing& dwg, const std::string& handle) {
    for (const auto& e : dwg.entities()) {
        if (e.props.handle == handle) return e;
    }
    for (const auto& b : dwg.blocks()) {
        for (const auto& e : b.entities) {
            if (e.props.handle == handle) return e;
        }
    }
    throw hdxf_test::AssertFailure{"no entity " + handle};
}

std::string target_error(const Drawing& dwg, DxfVersion v, const Drawing* original = nullptr) {
    return hdxf::internal::acis_target_error(dwg, v, original);
}

// A line of `length` characters of a SAT's kind (a long spline record on one line), enciphered
std::string long_sat_line(size_t length) {
    std::string line = "-80 intcurve-curve $-1 forward { exactcur nubs 3 open 56";
    int n = 0;
    while (line.size() < length) line += " " + std::to_string(n++) + ".015625";
    line.resize(length);
    // A line does not end with a space (the reader keeps it, but a SAT line would not)
    if (line.back() == ' ') line.back() = '0';
    return line;
}

std::vector<std::string> sat_with_long_lines(const std::vector<size_t>& lengths) {
    std::vector<std::string> plain = {"106 6 1 0", "-0 body $-1 $-1 $-1 $-1 #"};
    for (size_t n : lengths) plain.push_back(long_sat_line(n));
    std::vector<std::string> out;
    for (const auto& l : plain) out.push_back(encipher(l));
    return out;
}

std::string deciphered_of(const AcisData& d) { return hdxf::internal::acis_deciphered(d); }

std::string deciphered_lines(const std::vector<std::string>& enciphered) {
    AcisData d;
    d.lines = enciphered;
    return deciphered_of(d);
}

// The 3 and 1 groups of the ACIS lines in a file, in order: "3:<n> 1:<n>"
std::string acis_group_shape(const std::vector<uint8_t>& file) {
    std::istringstream in(std::string(file.begin(), file.end()), std::ios::binary);
    hdxf::internal::GroupReader r(in);
    r.detect_format();
    hdxf::internal::GroupCode gc;
    std::string shape;
    bool in_modeler = false;
    while (r.read(gc)) {
        if (gc.code == 100) in_modeler = gc.value == "AcDbModelerGeometry";
        if (!in_modeler) continue;
        if (gc.code == 3) shape += "3:" + std::to_string(gc.value.size()) + " ";
        if (gc.code == 1) shape += "1:" + std::to_string(gc.value.size()) + " ";
    }
    return shape;
}

// The 70, 1 and 3 groups of every body's modeler subclass, in the file's order, each "code:value"
std::vector<std::string> modeler_groups(const std::vector<uint8_t>& file) {
    std::istringstream in(std::string(file.begin(), file.end()), std::ios::binary);
    hdxf::internal::GroupReader r(in);
    r.detect_format();
    hdxf::internal::GroupCode gc;
    std::vector<std::string> out;
    bool in_modeler = false;
    while (r.read(gc)) {
        if (gc.code == 0) {
            in_modeler = false;
            continue;
        }
        if (gc.code == 100) in_modeler = gc.value == "AcDbModelerGeometry";
        if (in_modeler && (gc.code == 70 || gc.code == 1 || gc.code == 3)) {
            out.push_back(std::to_string(gc.code) + ":" + gc.value);
        }
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// The matrix
// ---------------------------------------------------------------------------

// 70 and the SAT header's number are two things; the SAT version is the header's first number
TEST(acis_r13_r14_sat_version_is_the_header) {
    AcisData d = data_of(small_sat(106));
    d.version = 1;                       // group 70
    int v = 0;
    hdxf_test::assert_true(hdxf::internal::acis_sat_version(d, v), "no version");
    hdxf_test::assert_eq(v, 106);
    d = data_of("400 112 1 0         \n16 Autodesk AutoCAD 20 ASM 230.7.0.65535 NT 0\n");
    hdxf_test::assert_true(hdxf::internal::acis_sat_version(d, v), "no version 400");
    hdxf_test::assert_eq(v, 400);
    // Spaces before the number (a tab in 1.x) and a header that is one number
    d = data_of("  21500\n");
    hdxf_test::assert_true(hdxf::internal::acis_sat_version(d, v));
    hdxf_test::assert_eq(v, 21500);
    // No lines, no number, a number followed by letters
    AcisData none;
    hdxf_test::assert_false(hdxf::internal::acis_sat_version(none, v));
    hdxf_test::assert_false(hdxf::internal::acis_sat_version(data_of("body $-1 #\n"), v));
    hdxf_test::assert_false(hdxf::internal::acis_sat_version(data_of("106x 1 1 0\n"), v));
    hdxf_test::assert_false(hdxf::internal::acis_sat_version(data_of("\n"), v));
}

TEST(acis_r13_r14_newest_sat) {
    hdxf_test::assert_eq(hdxf::internal::acis_newest_sat_version(DxfVersion::R13), 106);
    hdxf_test::assert_eq(hdxf::internal::acis_newest_sat_version(DxfVersion::R14), 106);
    // No limit here: R12 has no ACIS (its own error), 2000 … 2018 take what AutoCAD wrote
    for (DxfVersion v : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007,
                         DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018}) {
        hdxf_test::assert_eq(hdxf::internal::acis_newest_sat_version(v), 0);
    }
}

TEST(acis_r13_r14_target_matrix) {
    struct Case {
        int sat;
        bool r13;
        bool r14;
    };
    const Case cases[] = {
        {99, false, false},  {100, true, true},    {105, true, true},    {106, true, true},   {107, false, false},
        {200, false, false}, {300, false, false},  {400, false, false},  {700, false, false},
        {21200, false, false}, {21500, false, false}, {21800, false, false}, {22300, false, false},
    };
    for (const auto& c : cases) {
        for (EntityType type : {EntityType::Solid3D, EntityType::Region, EntityType::Body}) {
            Drawing dwg;
            dwg.add_acis(type, data_of(small_sat(c.sat)));
            std::string e13 = target_error(dwg, DxfVersion::R13);
            std::string e14 = target_error(dwg, DxfVersion::R14);
            hdxf_test::assert_eq(e13.empty() ? 1 : 0, c.r13 ? 1 : 0, "R13, SAT " + std::to_string(c.sat) + ": " + e13);
            hdxf_test::assert_eq(e14.empty() ? 1 : 0, c.r14 ? 1 : 0, "R14, SAT " + std::to_string(c.sat) + ": " + e14);
            if (!c.r13) {
                hdxf_test::assert_eq(e13, "ACIS SAT version " + std::to_string(c.sat) + " cannot be written as DXF R13");
            }
            if (!c.r14) {
                hdxf_test::assert_eq(e14, "ACIS SAT version " + std::to_string(c.sat) + " cannot be written as DXF R14");
            }
        }
    }
}

// The other versions have no limit from here; what each has is its own check
TEST(acis_r13_r14_target_other_versions) {
    Drawing dwg;
    dwg.add_acis(EntityType::Solid3D, data_of(small_sat(21500)));
    for (DxfVersion v : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018}) {
        hdxf_test::assert_eq(target_error(dwg, v), "");
    }
}

// ---------------------------------------------------------------------------
// Incompatible data
// ---------------------------------------------------------------------------

TEST(acis_r13_r14_target_errors) {
    // A body with only SAB (ACDSDATA, 2013+): no SAB → SAT conversion
    {
        Drawing dwg;
        AcisData sab;
        sab.sab = {'A', 'C', 'I', 'S', ' ', 'B', 'i', 'n', 'a', 'r', 'y', 'F', 'i', 'l', 'e'};
        dwg.add_acis(EntityType::Region, sab);
        hdxf_test::assert_eq(target_error(dwg, DxfVersion::R14), "A REGION with only SAB data cannot be written as DXF R14");
        hdxf_test::assert_eq(target_error(dwg, DxfVersion::R13), "A REGION with only SAB data cannot be written as DXF R13");
    }
    // Lines that are not SAT
    {
        Drawing dwg;
        AcisData text;
        text.lines = {encipher("not a save file")};
        dwg.add_acis(EntityType::Body, text);
        hdxf_test::assert_eq(target_error(dwg, DxfVersion::R14),
                             "A BODY's ACIS data is not SAT and cannot be written as DXF R14");
    }
    // 2013's change GUID: XDATA only in AutoCAD's 2000 … 2010 files
    {
        Drawing dwg;
        AcisData guid = data_of(small_sat(105));
        guid.guid = "{12345678-1234-1234-1234-123456789abc}";
        dwg.add_acis(EntityType::Solid3D, guid);
        hdxf_test::assert_eq(target_error(dwg, DxfVersion::R14), "A body's change GUID requires DXF 2000 or later");
    }
    // A body without data is nothing to check
    {
        Drawing dwg;
        dwg.add_acis(EntityType::Solid3D, AcisData{});
        hdxf_test::assert_eq(target_error(dwg, DxfVersion::R14), "");
    }
    // The first fault is the one named, bodies in blocks too
    {
        Drawing dwg;
        dwg.add_acis(EntityType::Solid3D, data_of(small_sat(106)));
        hdxf::Block& block = dwg.add_block("SOLIDS");
        hdxf::Entity e;
        e.type = EntityType::Region;
        e.data = data_of(small_sat(400));
        block.entities.push_back(e);
        hdxf_test::assert_eq(target_error(dwg, DxfVersion::R14), "ACIS SAT version 400 cannot be written as DXF R14");
        hdxf_test::assert_eq(target_error(dwg, DxfVersion::R13), "ACIS SAT version 400 cannot be written as DXF R13");
    }
}

// The header number is never changed to make a body fit: the check does not touch the drawing
TEST(acis_r13_r14_target_check_changes_nothing) {
    Drawing dwg;
    dwg.add_acis(EntityType::Solid3D, data_of(small_sat(400)));
    std::vector<std::string> before = dwg.entities()[0].as_acis().lines;
    hdxf_test::assert_false(target_error(dwg, DxfVersion::R14).empty());
    hdxf_test::assert_true(dwg.entities()[0].as_acis().lines == before, "lines changed");
    hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), 1);
}

// A body as it was read keeps its stream whatever its SAT: R14.01's ACIS 3.0 (SAT 300) in a file of
// AC1014, say — while it is the same; the same body changed or new is checked
TEST(acis_r13_r14_target_unchanged_read_body) {
    Drawing original;
    {
        AcisData d = data_of(small_sat(300));
        hdxf::Entity& e = original.add_acis(EntityType::Solid3D, d);
        e.props.handle = "2A";
        hdxf::Block& block = original.add_block("SOLIDS");
        hdxf::Entity b;
        b.type = EntityType::Body;
        b.props.handle = "2B";
        b.data = data_of(small_sat(400));
        block.entities.push_back(b);
    }
    Drawing same = original;
    hdxf_test::assert_eq(target_error(same, DxfVersion::R14, &original), "");
    hdxf_test::assert_eq(target_error(same, DxfVersion::R14), "ACIS SAT version 300 cannot be written as DXF R14");
    // Changed: one line
    Drawing changed = original;
    changed.entities()[0].as_acis().lines[3] += " ";
    hdxf_test::assert_eq(target_error(changed, DxfVersion::R14, &original), "ACIS SAT version 300 cannot be written as DXF R14");
    // The block's body
    Drawing block_changed = original;
    block_changed.blocks()[0].entities[0].as_acis().lines.push_back(encipher("-11 point $-1 0 0 0 #"));
    hdxf_test::assert_eq(target_error(block_changed, DxfVersion::R14, &original),
                         "ACIS SAT version 400 cannot be written as DXF R14");
    // Another handle is another body; a body without a handle is new
    Drawing renamed = original;
    renamed.entities()[0].props.handle = "2C";
    hdxf_test::assert_eq(target_error(renamed, DxfVersion::R14, &original), "ACIS SAT version 300 cannot be written as DXF R14");
    Drawing new_body = original;
    new_body.entities()[0].props.handle.clear();
    hdxf_test::assert_eq(target_error(new_body, DxfVersion::R14, &original), "ACIS SAT version 300 cannot be written as DXF R14");
}

// ---------------------------------------------------------------------------
// Reading R14 (the reader is the existing one): the stream comes back exactly
// ---------------------------------------------------------------------------

TEST(acis_r13_r14_read_bodies) {
    const std::vector<FileBody> bodies = {
        {"3DSOLID", "21", sat_lines(small_sat(105)), 1},
        {"REGION", "22", sat_lines(small_sat(106)), 1},
        {"BODY", "23", sat_lines(small_sat(106)), 1},
    };
    for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
        Drawing dwg = read_r14(r14_file(bodies, format));
        hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), 3);
        const EntityType types[] = {EntityType::Solid3D, EntityType::Region, EntityType::Body};
        for (size_t i = 0; i < bodies.size(); ++i) {
            const hdxf::Entity& e = body_with_handle(dwg, bodies[i].handle);
            hdxf_test::assert_true(e.type == types[i], bodies[i].type);
            hdxf_test::assert_true(e.as_acis().lines == bodies[i].lines, bodies[i].type + ": lines");
            hdxf_test::assert_eq(e.as_acis().version, 1, "group 70");
            hdxf_test::assert_true(e.as_acis().sab.empty() && e.as_acis().guid.empty());
            // The text the lines were made from
            std::string sat = deciphered_of(e.as_acis());
            hdxf_test::assert_eq(sat, small_sat(i == 0 ? 105 : 106), bodies[i].type + ": SAT");
            int v = 0;
            hdxf_test::assert_true(hdxf::internal::acis_sat_version(e.as_acis(), v));
            hdxf_test::assert_eq(v, i == 0 ? 105 : 106, "SAT version, not group 70");
        }
        // Both SAT versions are R13's and R14's (format profile D11)
        hdxf_test::assert_eq(target_error(dwg, DxfVersion::R14), "");
        hdxf_test::assert_eq(target_error(dwg, DxfVersion::R13), "");
    }
}

TEST(acis_r13_r14_read_bodies_in_a_block) {
    const std::vector<FileBody> in_block = {
        {"3DSOLID", "31", sat_lines(small_sat(106)), 1},
        {"REGION", "32", sat_lines(small_sat(105)), 1},
    };
    const std::vector<FileBody> model = {{"BODY", "33", sat_lines(small_sat(106)), 1}};
    for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
        Drawing dwg = read_r14(r14_file(model, format, in_block));
        hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), 1);
        const hdxf::Block* block = nullptr;
        for (const auto& b : dwg.blocks()) {
            if (b.name == "SOLIDS") block = &b;
        }
        hdxf_test::assert_true(block != nullptr, "no block");
        hdxf_test::assert_eq(static_cast<int>(block->entities.size()), 2);
        hdxf_test::assert_true(block->entities[0].type == EntityType::Solid3D);
        hdxf_test::assert_true(block->entities[1].type == EntityType::Region);
        hdxf_test::assert_eq(deciphered_of(block->entities[0].as_acis()), small_sat(106));
        hdxf_test::assert_eq(deciphered_of(block->entities[1].as_acis()), small_sat(105));
        hdxf_test::assert_eq(target_error(dwg, DxfVersion::R14), "");
        hdxf_test::assert_eq(target_error(dwg, DxfVersion::R13), "");
    }
}

// Lines of 254, 255, 256, 509, 510 and 600 characters: 3s of 255 and the rest — empty after a
// whole number of 255s — in 1; each line comes back as one line, the stream exactly
TEST(acis_r13_r14_read_long_lines) {
    const std::vector<size_t> lengths = {100, 254, 255, 256, 509, 510, 511, 600, 765, 3};
    FileBody b{"REGION", "41", sat_with_long_lines(lengths), 1};
    for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
        std::vector<uint8_t> file = r14_file({b}, format);
        Drawing dwg = read_r14(file);
        const AcisData& d = body_with_handle(dwg, "41").as_acis();
        hdxf_test::assert_true(d.lines == b.lines, "the lines are not the file's");
        hdxf_test::assert_eq(deciphered_of(d), deciphered_lines(b.lines));
        // The file has the shape: a 255 and its rest in 1, an empty 1 after a whole number of 255s
        std::string shape = acis_group_shape(file);
        hdxf_test::assert_true(shape.find("3:255 1:0 ") != std::string::npos, "255: " + shape);
        hdxf_test::assert_true(shape.find("3:255 3:255 1:0 ") != std::string::npos, "510: " + shape);
        hdxf_test::assert_true(shape.find("3:255 1:1 ") != std::string::npos, "256: " + shape);
        hdxf_test::assert_true(shape.find("1:254 ") != std::string::npos, "254: " + shape);
    }
}

// ---------------------------------------------------------------------------
// Other versions are not limited to SAT 105/106
// ---------------------------------------------------------------------------

TEST(acis_r13_r14_older_sat_in_2000) {
    for (int sat : {105, 106, 400}) {
        Drawing dwg(DxfVersion::R2000);
        dwg.add_acis(EntityType::Solid3D, data_of(small_sat(sat)));
        std::vector<uint8_t> out = dwg.save_to_memory();
        hdxf_test::assert_false(out.empty(), dwg.last_error());
        Drawing back;
        hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
        hdxf_test::assert_eq(static_cast<int>(back.entities().size()), 1);
        hdxf_test::assert_true(back.entities()[0].as_acis().lines == data_of(small_sat(sat)).lines);
        hdxf_test::assert_eq(back.entities()[0].as_acis().version, 1);
    }
}

// ---------------------------------------------------------------------------
// Writing R13 and R14 (the write path of steps 7.2–7.5)
// ---------------------------------------------------------------------------

TEST(acis_r13_r14_write_stream_exact) {
    for (DxfVersion version : {DxfVersion::R13, DxfVersion::R14}) {
        for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const std::string where = std::string(version == DxfVersion::R13 ? "R13 " : "R14 ") +
                                      (format == DxfFormat::Ascii ? "ASCII" : "binary");
            const int sat = version == DxfVersion::R13 ? 105 : 106;
            Drawing dwg(version);
            dwg.add_acis(EntityType::Solid3D, data_of(small_sat(sat)), "A");
            dwg.add_acis(EntityType::Region, data_of(small_sat(sat - 1)), "B");
            dwg.add_acis(EntityType::Body, data_of(small_sat(100)), "C");
            hdxf::Block& block = dwg.add_block("SOLIDS");
            hdxf::Entity e;
            e.type = EntityType::Body;
            e.data = data_of(small_sat(sat));
            block.entities.push_back(e);
            std::vector<uint8_t> out = dwg.save_to_memory(format);
            hdxf_test::assert_false(out.empty(), where + ": " + dwg.last_error());

            Drawing back;
            hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), where + ": " + back.last_error());
            hdxf_test::assert_true(back.version() == version, where + ": version");
            hdxf_test::assert_eq(static_cast<int>(back.entities().size()), 3, where);
            const int expect[] = {sat, sat - 1, 100};
            const EntityType types[] = {EntityType::Solid3D, EntityType::Region, EntityType::Body};
            for (size_t i = 0; i < 3; ++i) {
                const AcisData& d = back.entities()[i].as_acis();
                hdxf_test::assert_true(back.entities()[i].type == types[i], where + ": type");
                hdxf_test::assert_true(d.lines == data_of(small_sat(expect[i])).lines, where + ": lines");
                hdxf_test::assert_eq(d.version, 1, where + ": group 70");
                hdxf_test::assert_true(d.sab.empty() && d.guid.empty(), where);
            }
            const hdxf::Block* read_block = nullptr;
            for (const auto& b : back.blocks()) {
                if (b.name == "SOLIDS") read_block = &b;
            }
            hdxf_test::assert_true(read_block && read_block->entities.size() == 1, where + ": block");
            hdxf_test::assert_eq(deciphered_of(read_block->entities[0].as_acis()), small_sat(sat), where + ": block SAT");

            // No ACDSDATA, no SAB record, and the modeler subclass is there
            std::string text(out.begin(), out.end());
            hdxf_test::assert_true(text.find("ACDSDATA") == std::string::npos, where + ": ACDSDATA");
            hdxf_test::assert_true(text.find("AcDbModelerGeometry") != std::string::npos, where + ": subclass");
        }
    }
}

TEST(acis_r13_r14_write_long_lines) {
    const std::vector<size_t> lengths = {100, 254, 255, 256, 510, 600};
    for (DxfVersion version : {DxfVersion::R13, DxfVersion::R14}) {
        for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
            AcisData d;
            d.lines = sat_with_long_lines(lengths);
            d.lines[0] = encipher(version == DxfVersion::R13 ? "105 6 1 0" : "106 6 1 0");
            Drawing dwg(version);
            dwg.add_acis(EntityType::Region, d);
            std::vector<uint8_t> out = dwg.save_to_memory(format);
            hdxf_test::assert_false(out.empty(), dwg.last_error());
            Drawing back;
            hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
            hdxf_test::assert_eq(static_cast<int>(back.entities().size()), 1);
            hdxf_test::assert_true(back.entities()[0].as_acis().lines == d.lines, "long lines changed");
            std::string shape = acis_group_shape(out);
            hdxf_test::assert_true(shape.find("3:255 1:0 ") != std::string::npos, "255: " + shape);
            hdxf_test::assert_true(shape.find("3:255 3:255 1:0 ") != std::string::npos, "510: " + shape);
        }
    }
}

// A SAT newer than the target: the error names it, nothing is written, the drawing is as it was
TEST(acis_r13_r14_write_incompatible_sat) {
    for (DxfVersion version : {DxfVersion::R13, DxfVersion::R14}) {
        const std::string name = version == DxfVersion::R13 ? "R13" : "R14";
        for (int sat : {400, 21500}) {
            Drawing dwg(version);
            dwg.add_acis(EntityType::Solid3D, data_of(small_sat(sat)));
            hdxf::Entity& e = dwg.add_line({0, 0, 0}, {1, 1, 0});
            e.props.layer = "0";
            const std::vector<std::string> lines = dwg.entities()[0].as_acis().lines;
            for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
                std::vector<uint8_t> out = dwg.save_to_memory(format);
                hdxf_test::assert_true(out.empty(), "written");
                hdxf_test::assert_eq(dwg.last_error(),
                                     "ACIS SAT version " + std::to_string(sat) + " cannot be written as DXF " + name);
            }
            // The file: not created
            std::filesystem::path path = std::filesystem::temp_directory_path() /
                                         ("hdxf_acis_" + name + "_" + std::to_string(sat) + ".dxf");
            std::filesystem::remove(path);
            hdxf_test::assert_false(dwg.save_as(path), "saved");
            hdxf_test::assert_false(std::filesystem::exists(path), "a file was left");
            hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), 2);
            hdxf_test::assert_true(dwg.entities()[0].as_acis().lines == lines, "lines changed");
        }
    }
}

TEST(acis_r13_r14_write_sab_only_and_non_sat) {
    for (DxfVersion version : {DxfVersion::R13, DxfVersion::R14}) {
        const std::string name = version == DxfVersion::R13 ? "R13" : "R14";
        Drawing sab_only(version);
        AcisData sab;
        sab.sab = {'A', 'S', 'M', ' ', 'B', 'i', 'n', 'a', 'r', 'y', 'F', 'i', 'l', 'e', '4'};
        sab_only.add_acis(EntityType::Solid3D, sab);
        hdxf_test::assert_true(sab_only.save_to_memory().empty(), "SAB written");
        hdxf_test::assert_eq(sab_only.last_error(), "A 3DSOLID with only SAB data cannot be written as DXF " + name);

        Drawing text(version);
        AcisData lines;
        lines.lines = {encipher("hello")};
        text.add_acis(EntityType::Body, lines);
        hdxf_test::assert_true(text.save_to_memory().empty(), "text written");
        hdxf_test::assert_eq(text.last_error(), "A BODY's ACIS data is not SAT and cannot be written as DXF " + name);
    }
}

// A body read from an R14 file and written back as R14: its lines as they were, SAT 300 too
TEST(acis_r13_r14_write_read_body_unchanged) {
    const std::vector<FileBody> bodies = {
        {"3DSOLID", "51", sat_lines(small_sat(106)), 1},
        {"REGION", "52", sat_lines(small_sat(300)), 1},
        {"BODY", "53", sat_with_long_lines({100, 255, 600}), 1},
    };
    for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
        std::vector<uint8_t> file = r14_file(bodies, format);
        Drawing dwg = read_r14(file);
        std::vector<uint8_t> out = dwg.save_to_memory(format);
        hdxf_test::assert_false(out.empty(), dwg.last_error());
        // Each body's 70, 1 and 3 groups are the file's, in the file's order and shape (the file
        // around them is what steps 7.2-7.5 and 7.18 write)
        hdxf_test::assert_true(modeler_groups(out) == modeler_groups(file), "the bodies' groups are not the file's");
        Drawing back = read_r14(out);
        for (const auto& b : bodies) {
            hdxf_test::assert_true(body_with_handle(back, b.handle).as_acis().lines == b.lines, b.type);
        }
    }
}

// One changed property of a read body keeps its lines; a changed stream newer than R14 is an error
TEST(acis_r13_r14_write_read_body_changed) {
    const std::vector<FileBody> bodies = {{"REGION", "61", sat_lines(small_sat(300)), 1}};
    std::vector<uint8_t> file = r14_file(bodies, DxfFormat::Ascii);
    Drawing dwg = read_r14(file);
    // The layer: the stream is the file's
    for (auto& e : dwg.entities()) e.props.layer = "OTHER";
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    Drawing back = read_r14(out);
    hdxf_test::assert_true(body_with_handle(back, "61").as_acis().lines == bodies[0].lines);
    // The SAT: a body that is not the file's is checked
    for (auto& e : dwg.entities()) e.as_acis().lines.push_back(encipher("-11 point $-1 0 0 0 #"));
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "written");
    hdxf_test::assert_eq(dwg.last_error(), "ACIS SAT version 300 cannot be written as DXF R14");
}
