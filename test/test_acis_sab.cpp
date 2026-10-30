#include "test_framework.hpp"
#include "hdxf.hpp"
#include "acis.hpp"
#include "acis_sab.hpp"
#include "extents.hpp"
#include "group_io.hpp"
#include "dxf_structure.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// SAT → SAB (phase 5, step 5.5 — plan B62/B63): a body's SAT 21200/21500/21800 made into the SAB
// 21800 AutoCAD writes in 2013 — the topology, the analytic geometry and the attributes of
// AutoCAD's 21200/21500 bodies. Facit: AutoCAD's own SAB of the same bodies (ACadSharp's
// sample_AC1021/AC1024 against sample_AC1027, LibreDWG's example_2007/2010 against
// example_2013), byte for byte but the date (the SAT's is empty); the SAB read back gives the
// bodies and extents of the SAT.
// ---------------------------------------------------------------------------

namespace {

using hdxf::internal::AcisFile;
using hdxf::internal::Extents;

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

hdxf::Drawing load(const std::string& name) {
    std::vector<uint8_t> data = read_bytes(external(name));
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(data.data(), data.size()), name + ": " + dwg.last_error());
    return dwg;
}

const hdxf::AcisData* acis_of(const hdxf::Entity& e) {
    switch (e.type) {
        case hdxf::EntityType::Solid3D:
        case hdxf::EntityType::Region:
        case hdxf::EntityType::Body:
            return &e.as_acis();
        default:
            return nullptr;
    }
}

// The body with that handle, in the entities or a block
const hdxf::AcisData* body(const hdxf::Drawing& dwg, const std::string& handle) {
    for (const auto& e : dwg.entities()) {
        if (e.props.handle == handle) return acis_of(e);
    }
    for (const auto& b : dwg.blocks()) {
        for (const auto& e : b.entities) {
            if (e.props.handle == handle) return acis_of(e);
        }
    }
    return nullptr;
}

std::vector<uint8_t> encoded(const std::string& sat, const std::string& what,
                             int format = hdxf::internal::sab_format_acis) {
    std::vector<uint8_t> out;
    std::string error;
    hdxf_test::assert_true(hdxf::internal::sab_from_sat(sat, out, error, format), what + ": " + error);
    return out;
}

// The SAB without its date: the head's third string (15 bytes of magic, 16 of head, then the
// product, the ACIS version and the date, each tag 7 and a length byte)
std::vector<uint8_t> without_date(const std::vector<uint8_t>& sab) {
    size_t at = 31;
    for (int i = 0; i < 2 && at + 1 < sab.size(); ++i) at += 2 + sab[at + 1];
    if (at + 1 >= sab.size() || sab[at] != 7) return sab;
    std::vector<uint8_t> out(sab.begin(), sab.begin() + static_cast<std::ptrdiff_t>(at));
    out.insert(out.end(), sab.begin() + static_cast<std::ptrdiff_t>(at + 2 + sab[at + 1]), sab.end());
    return out;
}

std::string date_of(const std::vector<uint8_t>& sab) {
    size_t at = 31;
    for (int i = 0; i < 2 && at + 1 < sab.size(); ++i) at += 2 + sab[at + 1];
    if (at + 1 >= sab.size()) return {};
    return std::string(sab.begin() + static_cast<std::ptrdiff_t>(at + 2),
                       sab.begin() + static_cast<std::ptrdiff_t>(at + 2 + sab[at + 1]));
}

void same_bytes(const std::vector<uint8_t>& ours, const std::vector<uint8_t>& autocad, const std::string& what) {
    if (ours == autocad) return;
    size_t i = 0;
    while (i < ours.size() && i < autocad.size() && ours[i] == autocad[i]) ++i;
    hdxf_test::assert_true(false, what + ": differs from byte " + std::to_string(i) + " (" +
                                      std::to_string(ours.size()) + " against " + std::to_string(autocad.size()) + ")");
}

Extents box(const AcisFile& file) {
    Extents e;
    hdxf::internal::acis_extents(hdxf::internal::acis_bodies(file), hdxf::internal::AcisMap{}, e);
    return e;
}

// The SAB read back has the SAT's records, the same bodies and the same extents
void same_bodies(const std::string& sat, const std::string& what, int format = hdxf::internal::sab_format_acis) {
    std::vector<uint8_t> sab = encoded(sat, what, format);
    AcisFile from_sat, from_sab;
    hdxf_test::assert_true(hdxf::internal::read_sat(sat, from_sat), what + ": SAT read");
    hdxf_test::assert_true(hdxf::internal::read_sab(sab, from_sab), what + ": SAB read");
    hdxf_test::assert_eq(from_sab.version, format, what + ": version");
    // A SAT before 20800 has no asmheader: the SAB has it in front
    size_t front = !from_sat.records.empty() && from_sat.records[0].type == "asmheader" ? 0 : 1;
    hdxf_test::assert_eq(static_cast<int>(from_sab.records.size()), static_cast<int>(from_sat.records.size() + front),
                         what + ": records");
    if (front && !from_sab.records.empty()) hdxf_test::assert_eq(from_sab.records[0].type, std::string("asmheader"), what);
    for (size_t i = 0; i < from_sat.records.size() && i + front < from_sab.records.size(); ++i) {
        hdxf_test::assert_eq(from_sab.records[i + front].type, from_sat.records[i].type, what + ": record type");
    }
    std::vector<hdxf::internal::AcisBody> a = hdxf::internal::acis_bodies(from_sat);
    std::vector<hdxf::internal::AcisBody> b = hdxf::internal::acis_bodies(from_sab);
    hdxf_test::assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()), what + ": bodies");
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        hdxf_test::assert_eq(static_cast<int>(a[i].faces.size()), static_cast<int>(b[i].faces.size()), what + ": faces");
        hdxf_test::assert_eq(static_cast<int>(a[i].edges.size()), static_cast<int>(b[i].edges.size()), what + ": edges");
    }
    Extents ea = box(from_sat), eb = box(from_sab);
    hdxf_test::assert_true(!ea.empty() && !eb.empty(), what + ": extents");
    hdxf_test::assert_true(ea.min.x == eb.min.x && ea.min.y == eb.min.y && ea.min.z == eb.min.z &&
                               ea.max.x == eb.max.x && ea.max.y == eb.max.y && ea.max.z == eb.max.z,
                           what + ": the same extents");
}

std::string sat_text(std::initializer_list<const char*> lines) {
    std::string out;
    for (const char* l : lines) {
        out += l;
        out += '\n';
    }
    return out;
}

std::string sab_error(const std::string& sat) {
    std::vector<uint8_t> out;
    std::string error;
    hdxf_test::assert_false(hdxf::internal::sab_from_sat(sat, out, error), "no SAB");
    hdxf_test::assert_true(out.empty(), "nothing made");
    return error;
}

// A face and a wire in AutoCAD's 21500 form (the chain's wire, plan B63)
const char* const kHead[] = {"21500 0 2 24", "16 Autodesk AutoCAD 20 ASM 230.7.0.65535 NT 0",
                             "25.39999999999999858 9.999999999999999547e-07 1.000000000000000036e-10"};

std::string wire_body(const char* side) {
    std::string wire = std::string("wire $-1 -1 $-1 $-1 $5 $3 $-1 ") + side + " #";
    return sat_text({kHead[0], kHead[1], kHead[2], "asmheader $-1 -1 @13 230.7.0.65535 #",
                     "body $-1 -1 $-1 $2 $-1 $-1 #", "lump $-1 -1 $-1 $-1 $3 $1 #",
                     "shell $-1 -1 $-1 $-1 $-1 $-1 $4 $2 #", wire.c_str(),
                     "coedge $-1 -1 $-1 $5 $5 $-1 $6 forward $4 $-1 #",
                     "edge $-1 -1 $-1 $7 0 $8 10 $5 $9 forward @7 unknown #", "vertex $-1 -1 $-1 $6 0 $10 #",
                     "vertex $-1 -1 $-1 $6 1 $11 #", "straight-curve $-1 -1 $-1 0 0 0 1 0 0 F 0 F 10 #",
                     "point $-1 -1 $-1 0 0 0 #", "point $-1 -1 $-1 10 0 0 #", "End-of-ASM-data #"});
}

const char* const kSamples[] = {"acadsharp/sample_AC1021_ascii.dxf", "acadsharp/sample_AC1024_ascii.dxf",
                                "acadsharp/sample_AC1027_ascii.dxf"};
const char* const kLibre[] = {"libre/example_2007.dxf", "libre/example_2010.dxf", "libre/example_2013.dxf"};

} // namespace

// AutoCAD's SAT 21200 (2007) and 21500 (2010) of a drawing against its SAB 21800 (2013) of the
// same drawing: byte for byte but the date (ACadSharp's D65, D69, D6A; LibreDWG's 176, 2E1, 37D)
TEST(acis_sab_autocad_pairs) {
    struct Set {
        const char* const* files;
        std::vector<std::string> handles;
    };
    const Set sets[] = {{kSamples, {"D65", "D69", "D6A"}}, {kLibre, {"176", "2E1", "37D"}}};
    for (const auto& set : sets) {
        if (!has_files({set.files[0], set.files[1], set.files[2]})) return;
        hdxf::Drawing r2013 = load(set.files[2]);
        for (int v = 0; v < 2; ++v) {
            hdxf::Drawing older = load(set.files[v]);
            for (const auto& h : set.handles) {
                std::string what = std::string(set.files[v]) + " " + h;
                const hdxf::AcisData* sat = body(older, h);
                const hdxf::AcisData* sab = body(r2013, h);
                hdxf_test::assert_true(sat && sab && !sat->lines.empty() && !sab->sab.empty(), what + ": the pair");
                if (!sat || !sab) continue;
                std::vector<uint8_t> ours = encoded(hdxf::internal::acis_deciphered(*sat), what);
                hdxf_test::assert_eq(date_of(ours), std::string(), what + ": the SAT's empty date");
                hdxf_test::assert_false(date_of(sab->sab).empty(), what + ": AutoCAD's date");
                same_bytes(without_date(ours), without_date(sab->sab), what);
            }
        }
    }
}

// The SAB of every 21200/21500 body in the test files read back: the SAT's records, bodies and
// extents
TEST(acis_sab_read_back) {
    const char* const files[] = {"acadsharp/sample_AC1021_ascii.dxf", "acadsharp/sample_AC1021_binary.dxf",
                                 "acadsharp/sample_AC1024_ascii.dxf", "acadsharp/sample_AC1024_binary.dxf",
                                 "libre/example_2007.dxf", "libre/example_2010.dxf", "aspose/conic_pyramid.dxf"};
    int bodies = 0;
    for (const char* name : files) {
        if (!has_files({name})) return;
        hdxf::Drawing dwg = load(name);
        auto check = [&](const hdxf::Entity& e) {
            const hdxf::AcisData* d = acis_of(e);
            if (!d || d->lines.empty()) return;
            same_bodies(hdxf::internal::acis_deciphered(*d), std::string(name) + " " + e.props.handle);
            ++bodies;
        };
        for (const auto& e : dwg.entities()) check(e);
        for (const auto& b : dwg.blocks()) for (const auto& e : b.entities) check(e);
    }
    hdxf_test::assert_eq(bodies, 4 * 3 + 2 * 3 + 9, "the bodies");
}

// The record types without a pair in AutoCAD's files, by AutoCAD's chain (DXFIN, -WBLOCK, ODA
// ACAD2013 DXF; plan B66): a wire (side "out" false, "in" true), a face's containment
TEST(acis_sab_wire_and_face) {
    std::vector<uint8_t> out = encoded(wire_body("out"), "wire");
    // The wire's record as the chain's SAB has it: $ i $ $ $ $ $ F
    const uint8_t wire[] = {13, 4, 'w', 'i', 'r', 'e', 12, 0xFF, 0xFF, 0xFF, 0xFF, 4, 0xFF, 0xFF, 0xFF, 0xFF,
                            12, 0xFF, 0xFF, 0xFF, 0xFF, 12, 0xFF, 0xFF, 0xFF, 0xFF, 12, 5, 0, 0, 0,
                            12, 3, 0, 0, 0, 12, 0xFF, 0xFF, 0xFF, 0xFF, 11, 17};
    std::string bytes(out.begin(), out.end());
    hdxf_test::assert_true(bytes.find(std::string(std::begin(wire), std::end(wire))) != std::string::npos, "the wire");
    // The coedge in a wire: its integer 0 after the wire pointer
    const uint8_t coedge_tail[] = {12, 4, 0, 0, 0, 4, 0, 0, 0, 0, 12, 0xFF, 0xFF, 0xFF, 0xFF, 17};
    hdxf_test::assert_true(bytes.find(std::string(std::begin(coedge_tail), std::end(coedge_tail))) != std::string::npos,
                           "the coedge's integer");
    std::vector<uint8_t> in = encoded(wire_body("in"), "wire in");
    hdxf_test::assert_eq(static_cast<int>(in.size()), static_cast<int>(out.size()), "in");
    same_bodies(wire_body("out"), "wire");
    hdxf_test::assert_eq(sab_error(wire_body("inside")),
                         std::string("the ACIS record 4 (\"wire\") does not have the fields of its type in version 21500"),
                         "an unknown side");
}

// The end, the head and the identifiers as AutoCAD writes them
TEST(acis_sab_head_and_end) {
    std::vector<uint8_t> out = encoded(wire_body("out"), "wire");
    const char magic[] = "ACIS BinaryFile";
    hdxf_test::assert_true(out.size() > 31 && std::memcmp(out.data(), magic, 15) == 0, "magic");
    const uint8_t head[] = {0x28, 0x55, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 12, 0, 0, 0};
    hdxf_test::assert_true(std::memcmp(out.data() + 15, head, 16) == 0, "21800, 0, 2, 12");
    const uint8_t end[] = {14, 3, 'E', 'n', 'd', 14, 2, 'o', 'f', 14, 3, 'A', 'S', 'M', 13, 4, 'd', 'a', 't', 'a'};
    hdxf_test::assert_true(out.size() > sizeof end &&
                               std::memcmp(out.data() + out.size() - sizeof end, end, sizeof end) == 0,
                           "End-of-ASM-data");
    std::string bytes(out.begin(), out.end());
    hdxf_test::assert_true(bytes.find(std::string("\x0e\x08straight\x0d\x05" "curve", 17)) != std::string::npos,
                           "straight-curve as 14, 13");
    hdxf_test::assert_true(bytes.find(std::string("\x07\x10" "Autodesk AutoCAD\x07\x14" "ASM 230.7.0.65535 NT\x07\x00\x06", 43)) !=
                               std::string::npos,
                           "the product, the version, the empty date");
}

// What the encoder does not have: an error naming the version or the record type
TEST(acis_sab_errors) {
    std::string body = wire_body("out");
    auto with_version = [&](const char* v) { return std::string(v) + body.substr(5); };
    hdxf_test::assert_eq(sab_error(with_version("22300")), std::string("ACIS data of version 22300 cannot be written as DXF 2013"),
                         "a later version");
    hdxf_test::assert_eq(sab_error(with_version("99")), std::string("ACIS data of version 99 cannot be written as DXF 2013"),
                         "no ACIS version");
    hdxf_test::assert_eq(sab_error("21500 0 2 24\n"), std::string("the ACIS data cannot be read as SAT"), "no records");
    hdxf_test::assert_eq(sab_error("ACIS BinaryFile"), std::string("the ACIS data cannot be read as SAT"), "not SAT");

    std::string spline = body;
    spline.replace(spline.find("straight-curve"), 14, "intcurve-curve");
    hdxf_test::assert_eq(sab_error(spline),
                         std::string("the ACIS record 9 (\"intcurve-curve\") does not have the fields of its type in version 21500"),
                         "a spline curve with a straight line's fields");
    std::string unknown = body;
    unknown.replace(unknown.find("straight-curve"), 14, "helix-curve");
    hdxf_test::assert_eq(sab_error(unknown), std::string("the ACIS record \"helix-curve\" cannot be written as DXF 2013"),
                         "an unknown type");
    std::string misnamed = body;
    misnamed.replace(misnamed.find("asmheader"), 9, "xasmheader");
    hdxf_test::assert_eq(sab_error(misnamed), std::string("the ACIS record \"xasmheader\" cannot be written as DXF 2013"),
                         "an unknown first record");
    std::string short_point = body;
    short_point.replace(short_point.find("point $-1 -1 $-1 10 0 0"), 23, "point $-1 -1 $-1 10 0");
    hdxf_test::assert_eq(sab_error(short_point),
                         std::string("the ACIS record 11 (\"point\") does not have the fields of its type in version 21500"),
                         "a point of two numbers");
}

// A body with SAT lines in a new 2013 drawing: its SAB in ACDSDATA (AutoCAD's, but the date), the
// SAT lines not written; read back with the extents of the SAT; written as 2010 with its lines
TEST(acis_sab_new_drawing) {
    if (!has_files({kSamples[1], kSamples[2]})) return;
    hdxf::Drawing r2010 = load(kSamples[1]);
    hdxf::Drawing r2013 = load(kSamples[2]);
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
        std::vector<std::string> sources;
        for (const char* h : {"D65", "D69", "D6A"}) {
            for (const auto& e : r2010.entities()) {
                if (e.props.handle != h) continue;
                hdxf::AcisData d = e.as_acis();
                d.history_handle.clear();
                dwg.add_acis(e.type, d, e.props.layer);
                sources.push_back(h);
            }
        }
        hdxf_test::assert_eq(static_cast<int>(sources.size()), 3, "three bodies");
        std::vector<uint8_t> out = dwg.save_to_memory(format);
        hdxf_test::assert_false(out.empty(), dwg.last_error());
        if (format == hdxf::DxfFormat::Ascii) dxf_structure::check_structure(out, "ACadSharp's bodies as 2013");
        hdxf::Drawing back;
        hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
        size_t i = 0;
        for (const auto& e : back.entities()) {
            const hdxf::AcisData* d = acis_of(e);
            if (!d) continue;
            hdxf_test::assert_true(d->lines.empty(), "no SAT lines in 2013");
            const hdxf::AcisData* autocad = i < sources.size() ? body(r2013, sources[i]) : nullptr;
            hdxf_test::assert_true(autocad != nullptr, "AutoCAD's body");
            if (autocad) same_bytes(without_date(d->sab), without_date(autocad->sab), "the SAB of " + sources[i]);
            ++i;
        }
        hdxf_test::assert_eq(static_cast<int>(i), 3, "three bodies read back");
        // The extents of the SAB are those of the SAT
        for (const char* var : {"$EXTMIN", "$EXTMAX"}) {
            const hdxf::HeaderVar* a = back.find_header(var);
            hdxf::Drawing as_2010 = dwg;
            as_2010.set_version(hdxf::DxfVersion::R2010);
            std::vector<uint8_t> old = as_2010.save_to_memory(format);
            hdxf_test::assert_false(old.empty(), as_2010.last_error());
            hdxf::Drawing old_back;
            hdxf_test::assert_true(old_back.load_from_memory(old.data(), old.size()), old_back.last_error());
            const hdxf::HeaderVar* b = old_back.find_header(var);
            hdxf_test::assert_true(a && b && a->values == b->values, var);
            int with_lines = 0;
            for (const auto& e : old_back.entities()) {
                const hdxf::AcisData* d = acis_of(e);
                with_lines += d && !d->lines.empty();
            }
            hdxf_test::assert_eq(with_lines, 3, "2010: the SAT lines");
        }
    }
}

// The bodies of AutoCAD's 2013 file with their SAB replaced by the SAT of its 2010 file: the
// section as AutoCAD's but the dates — the records written anew with AutoCAD's groups
TEST(acis_sab_in_a_read_drawing) {
    if (!has_files({kSamples[1], kSamples[2]})) return;
    hdxf::Drawing r2010 = load(kSamples[1]);
    hdxf::Drawing dwg = load(kSamples[2]);
    for (auto& e : dwg.entities()) {
        if (e.type != hdxf::EntityType::Solid3D && e.type != hdxf::EntityType::Region) continue;
        const hdxf::AcisData* sat = body(r2010, e.props.handle);
        hdxf_test::assert_true(sat != nullptr, "the 2010 body " + e.props.handle);
        if (!sat) continue;
        e.as_acis().lines = sat->lines;
        e.as_acis().sab.clear();
    }
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    hdxf::Drawing autocad = load(kSamples[2]);
    int bodies = 0;
    for (const auto& e : back.entities()) {
        const hdxf::AcisData* d = acis_of(e);
        if (!d) continue;
        ++bodies;
        const hdxf::AcisData* a = body(autocad, e.props.handle);
        hdxf_test::assert_true(a && d->lines.empty(), e.props.handle);
        if (a) same_bytes(without_date(d->sab), without_date(a->sab), "the SAB of " + e.props.handle);
    }
    hdxf_test::assert_eq(bodies, 3, "the bodies");
}

namespace {

// The 1/3 groups of each AcDbModelerGeometry in an ASCII file, in order
std::vector<std::vector<std::pair<int, std::string>>> sat_groups(const std::vector<uint8_t>& data) {
    std::string dxf(data.begin(), data.end());
    size_t pos = 0;
    auto line = [&]() {
        size_t end = dxf.find('\n', pos);
        std::string s = dxf.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = end == std::string::npos ? dxf.size() : end + 1;
        while (!s.empty() && s.back() == '\r') s.pop_back();
        return s;
    };
    std::vector<std::vector<std::pair<int, std::string>>> out;
    bool in_body = false;
    while (pos < dxf.size()) {
        int code = std::atoi(line().c_str());
        std::string value = line();
        if (code == 0 || code == 100) in_body = false;
        if (code == 100 && value == "AcDbModelerGeometry") {
            in_body = true;
            out.emplace_back();
            continue;
        }
        if (in_body && (code == 1 || code == 3)) out.back().push_back({code, value});
    }
    return out;
}

} // namespace

// The SAT lines as AutoCAD writes them (plan 5.6): a line is its 3s and the 1 that ends it — a line
// of 255 characters or more in 3s of 255 and the rest in 1 (Aspose's conic_pyramid: "lump … #" in
// 1, eye_refinement of 255 in 3 and an empty 1). The file's bodies in a new 2010 drawing get the
// file's groups, group for group; each line read is one SAT record
TEST(acis_sat_lines_as_autocad) {
    const char* name = "aspose/conic_pyramid.dxf";
    if (!has_files({name})) return;
    std::vector<uint8_t> source = read_bytes(external(name));
    hdxf::Drawing src = load(name);
    hdxf::Drawing dwg(hdxf::DxfVersion::R2010);
    int bodies = 0, threes = 0;
    for (const auto& e : src.entities()) {
        const hdxf::AcisData* d = acis_of(e);
        if (!d) continue;
        ++bodies;
        // Each line one record: "#" (enciphered "|") only at its end
        for (const auto& l : d->lines) {
            size_t bar = l.find('|');
            hdxf_test::assert_true(bar == std::string::npos || bar + 1 == l.size(), "one record a line: " + l);
        }
        hdxf::AcisData copy = *d;
        copy.history_handle.clear();
        dwg.add_acis(e.type, copy, e.props.layer);
    }
    hdxf_test::assert_eq(bodies, 9, "conic_pyramid's bodies");
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    auto theirs = sat_groups(source);
    auto ours = sat_groups(out);
    hdxf_test::assert_eq(static_cast<int>(ours.size()), static_cast<int>(theirs.size()), "the bodies written");
    for (size_t i = 0; i < ours.size() && i < theirs.size(); ++i) {
        hdxf_test::assert_true(ours[i] == theirs[i], "body " + std::to_string(i) + ": AutoCAD's 1/3 groups");
        for (const auto& g : theirs[i]) threes += g.first == 3;
    }
    hdxf_test::assert_eq(threes, 9, "a 3 in each body");

    // Built: a line of exactly 255 characters is a 3 and an empty 1, one of 600 3, 3, 1
    hdxf::Drawing built(hdxf::DxfVersion::R2010);
    hdxf::AcisData acis;
    acis.lines = {"short", std::string(255, 'a'), std::string(600, 'b'), std::string(254, 'c')};
    built.add_acis(hdxf::EntityType::Region, acis);
    std::vector<uint8_t> b = built.save_to_memory();
    auto g = sat_groups(b);
    hdxf_test::assert_eq(static_cast<int>(g.size()), 1, "one body");
    if (g.size() != 1) return;
    std::vector<std::pair<int, std::string>> expected = {
        {1, "short"}, {3, std::string(255, 'a')}, {1, ""}, {3, std::string(255, 'b')}, {3, std::string(255, 'b')},
        {1, std::string(90, 'b')}, {1, std::string(254, 'c')}};
    hdxf_test::assert_true(g[0] == expected, "3s of 255, the rest in 1");
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(b.data(), b.size()), back.last_error());
    hdxf_test::assert_true(back.entities().at(0).as_acis().lines == acis.lines, "read back as the lines");
    // The reference's form (a 1 of 255 characters, its 3s after it — HDxf up to 1.5 wrote it so)
    // is read as the same line
    std::string text(b.begin(), b.end());
    std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string bs = std::string(255, 'b');
    std::string autocad_form = "  3" + eol + bs + eol + "  3" + eol + bs + eol + "  1" + eol + std::string(90, 'b') + eol;
    std::string reference_form = "  1" + eol + bs + eol + "  3" + eol + bs + eol + "  3" + eol + std::string(90, 'b') + eol;
    size_t at = text.find(autocad_form);
    hdxf_test::assert_true(at != std::string::npos, "AutoCAD's form in the file");
    if (at == std::string::npos) return;
    text.replace(at, autocad_form.size(), reference_form);
    hdxf::Drawing reference;
    hdxf_test::assert_true(reference.load_from_memory(reinterpret_cast<const uint8_t*>(text.data()), text.size()),
                           reference.last_error());
    hdxf_test::assert_true(reference.entities().at(0).as_acis().lines == acis.lines, "the reference's form read");
    std::vector<uint8_t> bin = built.save_to_memory(hdxf::DxfFormat::Binary);
    hdxf::Drawing bin_back;
    hdxf_test::assert_true(bin_back.load_from_memory(bin.data(), bin.size()), bin_back.last_error());
    hdxf_test::assert_true(bin_back.entities().at(0).as_acis().lines == acis.lines, "binary read back");
}

// ---------------------------------------------------------------------------
// Step 5.6: the spline geometry and the other attributes
// ---------------------------------------------------------------------------

namespace {

struct SabFacit {
    const char* label;
    const char* file;           // the manifest file whose own SAB is the facit; nullptr: sab below
    const char* handle;
    const char* sat;
    const char* sab;            // hex
};

const SabFacit kSabFacit[] = {
#include "acis_sab_facit.inc"
};

std::vector<uint8_t> from_hex(const char* hex) {
    std::vector<uint8_t> out;
    for (const char* p = hex; p[0] && p[1]; p += 2) {
        char pair[3] = {p[0], p[1], '\0'};
        out.push_back(static_cast<uint8_t>(std::strtoul(pair, nullptr, 16)));
    }
    return out;
}

// Where the records after asmheader start: after the end (17) of asmheader's record
size_t after_asmheader(const std::vector<uint8_t>& sab) {
    const std::string name("\x0d\x09" "asmheader", 11);
    std::string s(sab.begin(), sab.end());
    size_t at = s.find(name);
    if (at == std::string::npos) return sab.size();
    // p i S: a pointer (5 bytes), an integer (5), a string (tag, length, bytes), then 17
    at += name.size() + 5 + 5;
    if (at + 1 >= sab.size()) return sab.size();
    at += 2 + sab[at + 1];
    return at < sab.size() && sab[at] == 17 ? at + 1 : sab.size();
}

// The SAB against the facit: the head's four integers, and every byte after asmheader (the head's
// strings and asmheader's version are the writer's) but the sign of a zero: every SAT here is ODA's,
// and ODA writes -0 as "0" (none of its SAT has "-0"; its SAB of the same DWG has -0).
// `oda_version` (AutoCAD's own SAB of the body ODA's SAT was made from): the integer after
// "cyl_spl_sur" may differ — ODA writes 21201 for AutoCAD's 22200/22800 (AutoCAD's chain keeps the
// SAT's, the 21500 facit)
void same_records(const std::vector<uint8_t>& ours, const std::vector<uint8_t>& facit, bool oda_version,
                  const std::string& what) {
    hdxf_test::assert_true(ours.size() > 31 && facit.size() > 31 && std::memcmp(ours.data() + 15, facit.data() + 15, 16) == 0,
                           what + ": the head");
    size_t a = after_asmheader(ours), b = after_asmheader(facit);
    hdxf_test::assert_true(a < ours.size() && b < facit.size(), what + ": asmheader");
    const std::string cyl("\x0d\x0b" "cyl_spl_sur\x04", 14);
    while (a < ours.size() && b < facit.size()) {
        if (ours[a] == facit[b]) {
            ++a;
            ++b;
            continue;
        }
        bool version = false;
        for (size_t k = 0; k < 4 && oda_version && !version; ++k) {
            // ours[a] is byte k of the integer: the 14 bytes before the integer are cyl
            version = a >= k + cyl.size() &&
                      std::string(ours.begin() + static_cast<std::ptrdiff_t>(a - k - cyl.size()),
                                  ours.begin() + static_cast<std::ptrdiff_t>(a - k)) == cyl;
        }
        // The last byte of a real 0 or -0 (seven zero bytes before it; alone or in a position)
        bool zero = (ours[a] ^ facit[b]) == 0x80 && (ours[a] & 0x7F) == 0 && a >= 8 && b >= 8;
        for (size_t k = 1; k < 8 && zero; ++k) zero = ours[a - k] == 0 && facit[b - k] == 0;
        if (!version && !zero) break;
        ++a;
        ++b;
    }
    hdxf_test::assert_true(a == ours.size() && b == facit.size(),
                           what + ": differs at byte " + std::to_string(a) + " (" + std::to_string(ours.size()) +
                               " against " + std::to_string(facit.size()) + ")");
}

// SAB values for the expected records below
std::string b_int(int32_t v) {
    std::string s(1, '\x04');
    for (int i = 0; i < 4; ++i) s += static_cast<char>((static_cast<uint32_t>(v) >> (8 * i)) & 0xFF);
    return s;
}
std::string b_ptr(int32_t v) { return std::string(1, '\x0c') + b_int(v).substr(1); }
std::string b_enum(int32_t v) { return std::string(1, '\x15') + b_int(v).substr(1); }
std::string b_str(const std::string& v) { return std::string(1, '\x07') + static_cast<char>(v.size()) + v; }
std::string b_text(const std::string& v) {
    return std::string(1, '\x12') + b_int(static_cast<int32_t>(v.size())).substr(1) + v;
}
std::string b_real(double v) {
    uint64_t u;
    std::memcpy(&u, &v, sizeof u);
    std::string s(1, '\x06');
    for (int i = 0; i < 8; ++i) s += static_cast<char>((u >> (8 * i)) & 0xFF);
    return s;
}
std::string b_name(const std::string& type) {
    std::string s;
    size_t a = 0;
    while (true) {
        size_t dash = type.find('-', a);
        bool last = dash == std::string::npos;
        std::string part = type.substr(a, last ? std::string::npos : dash - a);
        s += static_cast<char>(last ? 13 : 14);
        s += static_cast<char>(part.size());
        s += part;
        if (last) break;
        a = dash + 1;
    }
    return s;
}
std::string b_word(const std::string& w) { return std::string(1, '\x0d') + static_cast<char>(w.size()) + w; }
const std::string kEnd(1, '\x11');
// An attribute's ENTITY and ATTRIB data as in attributed_body(): $-1 -1 $-1 $-1 $1
std::string b_attrib_head() { return b_ptr(-1) + b_int(-1) + b_ptr(-1) + b_ptr(-1) + b_ptr(1); }

// A body with one attribute record after it (21500, AutoCAD's head)
std::string attributed_body(const std::string& attribute) {
    std::string a = attribute + " #";
    return sat_text({kHead[0], kHead[1], kHead[2], "asmheader $-1 -1 @13 230.7.0.65535 #",
                     "body $2 -1 $-1 $-1 $-1 $-1 #", a.c_str(), "End-of-ASM-data #"});
}

bool has_bytes(const std::vector<uint8_t>& sab, const std::string& bytes) {
    return std::string(sab.begin(), sab.end()).find(bytes) != std::string::npos;
}

std::string custom_head(const char* def) { return b_name("ATTRIB_CUSTOM-attrib") + b_attrib_head() + b_str(def); }

const hdxf::AcisData* body_of(const hdxf::Entity& e) {
    if (const hdxf::AcisData* d = acis_of(e)) return d;
    if (e.type == hdxf::EntityType::Surface || e.type == hdxf::EntityType::ExtrudedSurface ||
        e.type == hdxf::EntityType::LoftedSurface || e.type == hdxf::EntityType::RevolvedSurface ||
        e.type == hdxf::EntityType::SweptSurface || e.type == hdxf::EntityType::PlaneSurface) {
        return &e.as_surface().acis;
    }
    return nullptr;
}

// One SAB value: its tag and its bytes after the tag
struct SabValue {
    uint8_t tag = 0;
    std::string bytes;
};

// The values of a SAB after its head (15 bytes of magic, 16 of head); false when it is not SAB
bool sab_values(const std::vector<uint8_t>& sab, std::vector<SabValue>& out) {
    out.clear();
    size_t p = 31;
    auto u32 = [&](size_t at) {
        return static_cast<uint32_t>(sab[at]) | (static_cast<uint32_t>(sab[at + 1]) << 8) |
               (static_cast<uint32_t>(sab[at + 2]) << 16) | (static_cast<uint32_t>(sab[at + 3]) << 24);
    };
    while (p < sab.size()) {
        uint8_t tag = sab[p++];
        size_t n = 0;
        switch (tag) {
            case 4: case 12: case 21: n = 4; break;
            case 6: case 23: n = 8; break;
            case 7: n = p < sab.size() ? 1 + sab[p] : 1; break;
            case 8: n = p + 1 < sab.size() ? 2 + (sab[p] | (sab[p + 1] << 8)) : 2; break;
            case 9: case 18: n = p + 3 < sab.size() ? 4 + u32(p) : 4; break;
            case 10: case 11: case 15: case 16: case 17: n = 0; break;
            case 13: case 14: n = p < sab.size() ? 1 + sab[p] : 1; break;
            case 19: case 20: n = 24; break;
            default: return false;
        }
        if (p + n > sab.size()) return false;
        out.push_back({tag, std::string(sab.begin() + static_cast<std::ptrdiff_t>(p),
                                        sab.begin() + static_cast<std::ptrdiff_t>(p + n))});
        p += n;
    }
    return true;
}

double real_at(const std::string& bytes, size_t at) {
    double v;
    std::memcpy(&v, bytes.data() + at, sizeof v);
    return v;
}

// The SAB against a facit of AutoCAD's chain of a SAT before 21200 (plan 5.7): record for record,
// paired by their pointers from the body (the chain writes the records in its own order for some),
// value for value but the asmheader (its version the writer's) — a real equal but in its last
// digits (1e-12 of it: what AutoCAD's kernel works out again from the old data — an edge's
// parameter projected anew, an ellipse's axes made unit, "-0")
void same_values(const std::vector<uint8_t>& ours, const std::vector<uint8_t>& facit, const std::string& what) {
    hdxf_test::assert_true(ours.size() > 31 && facit.size() > 31 && std::memcmp(ours.data() + 15, facit.data() + 15, 16) == 0,
                           what + ": the head");
    std::vector<SabValue> a, b;
    hdxf_test::assert_true(sab_values(ours, a) && sab_values(facit, b), what + ": SAB");
    // The records: from after the head's three strings and three reals, each to its end (17)
    auto records = [](const std::vector<SabValue>& v) {
        std::vector<std::vector<SabValue>> out(1);
        for (size_t i = 6; i < v.size(); ++i) {
            out.back().push_back(v[i]);
            if (v[i].tag == 17) out.emplace_back();
        }
        if (out.back().empty()) out.pop_back();
        return out;
    };
    std::vector<std::vector<SabValue>> ra = records(a), rb = records(b);
    hdxf_test::assert_eq(static_cast<int>(ra.size()), static_cast<int>(rb.size()), what + ": the records");
    // Paired from the body (record 1) by the pointers where they stand: the chain writes the
    // records in its own order
    std::map<int32_t, int32_t> pair{{1, 1}};
    std::vector<std::pair<int32_t, int32_t>> queue{{1, 1}};
    for (size_t q = 0; q < queue.size(); ++q) {
        auto [i, j] = queue[q];
        if (i < 0 || j < 0 || static_cast<size_t>(i) >= ra.size() || static_cast<size_t>(j) >= rb.size()) continue;
        const auto& x = ra[static_cast<size_t>(i)];
        const auto& y = rb[static_cast<size_t>(j)];
        bool same = x.size() == y.size();
        for (size_t k = 0; same && k < x.size(); ++k) {
            if (x[k].tag != y[k].tag) {
                same = false;
            } else if (x[k].tag == 12) {
                int32_t p, r;
                std::memcpy(&p, x[k].bytes.data(), 4);
                std::memcpy(&r, y[k].bytes.data(), 4);
                if ((p < 0) != (r < 0)) same = false;
                else if (p >= 0) {
                    auto it = pair.find(p);
                    if (it == pair.end()) {
                        pair[p] = r;
                        queue.push_back({p, r});
                    } else if (it->second != r) {
                        same = false;
                    }
                }
            } else if (x[k].bytes != y[k].bytes) {
                same = x[k].tag == 6 || x[k].tag == 19 || x[k].tag == 20;
                for (size_t at = 0; same && at + 8 <= x[k].bytes.size(); at += 8) {
                    double u = real_at(x[k].bytes, at), w = real_at(y[k].bytes, at);
                    same = std::fabs(u - w) <= 1e-12 * (1.0 + std::fabs(w));
                }
            }
        }
        hdxf_test::assert_true(same, what + ": record " + std::to_string(i) + " against the facit's " + std::to_string(j));
        if (!same) return;
    }
    // All but the asmheader and the end
    hdxf_test::assert_eq(static_cast<int>(pair.size()), static_cast<int>(ra.size()) - 2, what + ": every record paired");
}

int sat_version(const char* sat) { return std::atoi(sat); }

} // namespace

// The facit (test/acis_sab_facit.inc, tools/gen_sab_facit.py): the SAT ODA makes of jscad's 3Dlines
// (cyl_spl_sur, exact_int_cur, exp_par_cur, "ref") against AutoCAD's own SAB of it; and the SAT
// 21500 and 21200 ODA makes of AutoCAD's chain of 3Dlines, LibreDWG's Surface_2004 (exact_spl_sur)
// and ezdxf's 3dsolids (material, integer attributes) against the SAB of AutoCAD's chain — every
// byte after asmheader. The versions before 21200 (5.7, same_values()): ACadSharp's 2000 (SAT 400)
// and 2004 (20800) bodies, LibreDWG's Surface_2004 (20800) and test_acis.cpp's 7.0 bodies against
// AutoCAD's chain of a 2010 drawing of them, ezdxf's 3dsolids (700) against ODA's ACAD2013 DXF
TEST(acis_sab_facit) {
    int checked = 0, old = 0;
    for (const auto& f : kSabFacit) {
        if (sat_version(f.sat) < 21200) {
            same_values(encoded(f.sat, f.label), from_hex(f.sab), f.label);
            ++old;
            continue;
        }
        std::vector<uint8_t> ours = encoded(f.sat, f.label);
        if (ours.empty()) continue;
        std::vector<uint8_t> facit;
        if (f.file) {
            if (!has_files({f.file})) return;
            hdxf::Drawing dwg = load(f.file);
            const hdxf::AcisData* d = nullptr;
            for (const auto& e : dwg.entities()) {
                if (e.props.handle == f.handle) d = body_of(e);
            }
            hdxf_test::assert_true(d != nullptr, std::string(f.label) + ": AutoCAD's body");
            if (!d) continue;
            facit = d->sab;
        } else {
            facit = from_hex(f.sab);
        }
        same_records(ours, facit, f.file != nullptr, f.label);
        ++checked;
    }
    hdxf_test::assert_eq(checked, 5 + 5 + 5 + 5 + 5, "the facit's bodies");
    hdxf_test::assert_eq(old, 3 + 3 + 5 + 5 + 7, "the facit's bodies before 21200");
}

// The facit's SAB read back: the SAT's records, bodies and extents (spline surfaces and curves,
// pcurves); 3Dlines' extents those of AutoCAD's own SAB
TEST(acis_sab_facit_read_back) {
    for (const auto& f : kSabFacit) {
        same_bodies(f.sat, f.label);
        if (!f.file || !has_files({f.file})) continue;
        hdxf::Drawing dwg = load(f.file);
        for (const auto& e : dwg.entities()) {
            const hdxf::AcisData* d = e.props.handle == f.handle ? body_of(e) : nullptr;
            if (!d) continue;
            AcisFile ours, autocad;
            hdxf_test::assert_true(hdxf::internal::read_sab(encoded(f.sat, f.label), ours) &&
                                       hdxf::internal::read_sab(d->sab, autocad),
                                   f.label);
            Extents a = box(ours), b = box(autocad);
            hdxf_test::assert_true(a.min.x == b.min.x && a.min.y == b.min.y && a.min.z == b.min.z &&
                                       a.max.x == b.max.x && a.max.y == b.max.y && a.max.z == b.max.z,
                                   std::string(f.label) + ": AutoCAD's extents");
        }
    }
}

// The attributes AutoCAD's kernel does not know, as its chain keeps them: the strings typed, the
// text between them as it stands (kolja's rgb_color, klonk's id-tsl with its two spaces, ODA's
// lower-case attrib_custom of apollo_3d)
TEST(acis_sab_unknown_attributes) {
    std::vector<uint8_t> rgb = encoded(attributed_body("rgb_color-st-attrib $-1 -1 $-1 $-1 $1 0 1 0"), "rgb");
    hdxf_test::assert_true(has_bytes(rgb, b_name("rgb_color-st-attrib") + b_attrib_head() + b_text("0 1 0 ") + kEnd),
                           "rgb_color");
    std::vector<uint8_t> tsl = encoded(attributed_body("id-tsl-attrib $-1 -1 $-1 $-1 $1 400250 "), "id-tsl");
    hdxf_test::assert_true(has_bytes(tsl, b_name("id-tsl-attrib") + b_attrib_head() + b_text("400250  ") + kEnd),
                           "id-tsl");
    std::vector<uint8_t> custom = encoded(attributed_body("attrib_custom-attrib $-1 -1 $-1 $-1 $1 @22 generic_tag_attrib_def "
                                                          "3 3 -1 @23 generic_tag_attrib_def  1 3 @3 304 7 0 0"),
                                          "attrib_custom");
    hdxf_test::assert_true(has_bytes(custom, b_name("attrib_custom-attrib") + b_attrib_head() + b_str("generic_tag_attrib_def") +
                                                 b_text("3 3 -1 ") + b_str("generic_tag_attrib_def ") + b_text("1 3 ") +
                                                 b_str("304") + b_text("7 0 0 ") + kEnd),
                           "attrib_custom");
    hdxf_test::assert_eq(sab_error(attributed_body("rgb_color-st-attrib $-1 -1 $-1 $-1 $1 0 $3 0")),
                         std::string("the ACIS record 2 (\"rgb_color-st-attrib\") does not have the fields of its type in "
                                     "version 21500"),
                         "a pointer in unknown data");
}

// The attributes as AutoCAD's own SAB has them: ATTRIB_CUSTOM (apollo_3d, the f360 files: the time
// stamp an integer and a real, the tags and sketches integers and strings), the name attributes'
// actions (jerwil's name_attrib "lose lose apply copy" → 0 0 2 2, the 3dsolids' integer_attrib
// "copy custom ignore copy" → 2 5 1 2), the material
TEST(acis_sab_known_attributes) {
    std::vector<uint8_t> stamp = encoded(
        attributed_body("ATTRIB_CUSTOM-attrib $-1 -1 $-1 $-1 $1 @20 Timestamp_attrib_def 1 1788555391449218"), "stamp");
    hdxf_test::assert_true(has_bytes(stamp, custom_head("Timestamp_attrib_def") + b_int(1) + b_real(1788555391449218.0) + kEnd),
                           "Timestamp_attrib_def");
    std::vector<uint8_t> tag = encoded(attributed_body("ATTRIB_CUSTOM-attrib $-1 -1 $-1 $-1 $1 @22 generic_tag_attrib_def "
                                                       "3 3 -1 @23 generic_tag_attrib_def  1 1 @1 6 0 1 304 0"),
                                       "tag");
    hdxf_test::assert_true(has_bytes(tag, custom_head("generic_tag_attrib_def") + b_int(3) + b_int(3) + b_int(-1) +
                                              b_str("generic_tag_attrib_def ") + b_int(1) + b_int(1) + b_str("6") +
                                              b_int(0) + b_int(1) + b_int(304) + b_int(0) + kEnd),
                           "generic_tag_attrib_def");
    std::vector<uint8_t> sketch = encoded(
        attributed_body("ATTRIB_CUSTOM-attrib $-1 -1 $-1 $-1 $1 @17 sketch_attrib_def 1 1 3 @13 126 0 0 0 1 1"), "sketch");
    hdxf_test::assert_true(has_bytes(sketch, custom_head("sketch_attrib_def") + b_int(1) + b_int(1) + b_int(3) +
                                                 b_str("126 0 0 0 1 1") + kEnd),
                           "sketch_attrib_def");
    hdxf_test::assert_eq(sab_error(attributed_body("ATTRIB_CUSTOM-attrib $-1 -1 $-1 $-1 $1 @9 other_def 1")),
                         std::string("the ACIS attribute definition \"other_def\" (record 2, \"ATTRIB_CUSTOM-attrib\") "
                                     "cannot be written as DXF 2013"),
                         "an unknown definition");

    std::vector<uint8_t> name = encoded(
        attributed_body("name_attrib-gen-attrib $-1 -1 $-1 $-1 $1 lose lose apply copy @14 sweep_end_face"), "name");
    hdxf_test::assert_true(has_bytes(name, b_name("name_attrib-gen-attrib") + b_attrib_head() + b_enum(0) + b_enum(0) +
                                               b_enum(2) + b_enum(2) + b_str("sweep_end_face") + kEnd),
                           "name_attrib");
    std::vector<uint8_t> integer = encoded(attributed_body("integer_attrib-name_attrib-gen-attrib $-1 -1 $-1 $-1 $1 "
                                                           "copy custom ignore copy @7 bdm_uid 20"),
                                           "integer");
    hdxf_test::assert_true(has_bytes(integer, b_name("integer_attrib-name_attrib-gen-attrib") + b_attrib_head() + b_enum(2) +
                                                  b_enum(5) + b_enum(1) + b_enum(2) + b_str("bdm_uid") + b_int(20) + kEnd),
                           "integer_attrib");
    std::vector<uint8_t> material = encoded(attributed_body("material-adesk-attrib $-1 -1 $-1 $-1 $1 69 0"), "material");
    hdxf_test::assert_true(has_bytes(material, b_name("material-adesk-attrib") + b_attrib_head() + b_int(69) + b_int(0) + kEnd),
                           "material");
    hdxf_test::assert_eq(sab_error(attributed_body("name_attrib-gen-attrib $-1 -1 $-1 $-1 $1 split lose apply copy @1 x")),
                         std::string("the ACIS word \"split\" (record 2, \"name_attrib-gen-attrib\") cannot be written as DXF 2013"),
                         "an action no oracle has");
}

// A spline curve's bs3 data; what the oracles do not have — a subtype, a word, the tolerant records
// (AutoCAD's kernel does not read the only body with them, speaker.dxf: error 75004)
TEST(acis_sab_spline_errors) {
    std::string body = wire_body("out");
    const std::string straight = "straight-curve $-1 -1 $-1 0 0 0 1 0 0 F 0 F 10";
    std::string exact = body;
    exact.replace(exact.find(straight), straight.size(),
                  "intcurve-curve $-1 -1 $-1 forward { exact_int_cur 22800 full nubs 1 open 2 0 1 10 1 0 0 0 10 0 0 0 "
                  "null_surface null_surface nullbs nullbs I I 0 0 0 0 F 1 F 0 EXTEND_213 EXTEND_213 } F 0 F 10");
    std::vector<uint8_t> line = encoded(exact, "an exact spline curve");
    hdxf_test::assert_true(has_bytes(line, b_word("exact_int_cur") + b_int(22800) + b_enum(0) + b_word("nubs") + b_int(1) +
                                               b_enum(0) + b_int(2) + b_real(0) + b_int(1) + b_real(10) + b_int(1) +
                                               b_real(0) + b_real(0) + b_real(0) + b_real(10) + b_real(0) + b_real(0) +
                                               b_real(0) + b_word("null_surface") + b_word("null_surface") +
                                               b_word("nullbs") + b_word("nullbs")),
                           "the bs3 curve: the knots and the control points");
    same_bodies(exact, "an exact spline curve");

    // A pcurve's 3D curve (the chain's, of a tolerant coedge): the int_cur data and two logicals
    std::string par = exact;
    par.replace(par.find("exact_int_cur 22800"), 19, "par_int_cur 23100");
    par.replace(par.find("I I 0 0 0 0 F 1 F 0 EXTEND_213 EXTEND_213 }"), 43, "I I 0 0 0 0 T F }");
    hdxf_test::assert_true(has_bytes(encoded(par, "par_int_cur"), b_word("par_int_cur") + b_int(23100) + b_enum(0)),
                           "par_int_cur");
    hdxf_test::assert_true(has_bytes(encoded(par, "par_int_cur"),
                                     b_word("nullbs") + "\x0b\x0b" + b_int(0) + b_int(0) + b_int(0) + b_int(0) + "\x0a\x0b\x10"),
                           "par_int_cur's logicals");
    // The closures and singularities no body has, by ODA's words for them (B63): closed 1
    std::string closed = exact;
    closed.replace(closed.find("open"), 4, "closed");
    hdxf_test::assert_true(has_bytes(encoded(closed, "closed"), b_word("nubs") + b_int(1) + b_enum(1)), "closed");
    std::string summary = exact;
    summary.replace(summary.find("full"), 4, "summary");
    hdxf_test::assert_eq(sab_error(summary),
                         std::string("the ACIS word \"summary\" (record 9, \"intcurve-curve\") cannot be written as DXF 2013"),
                         "an approximation no oracle has");
    std::string surface_curve = exact;
    surface_curve.replace(surface_curve.find("exact_int_cur"), 13, "surf_int_cur");
    hdxf_test::assert_eq(sab_error(surface_curve),
                         std::string("the ACIS subtype \"surf_int_cur\" (record 9, \"intcurve-curve\") cannot be written as DXF 2013"),
                         "a subtype AutoCAD's core does not read");
    std::string tolerant = body;
    tolerant.replace(tolerant.find("coedge $-1"), 6, "tcoedge-coedge");
    hdxf_test::assert_eq(sab_error(tolerant),
                         std::string("the ACIS record 5 (\"tcoedge-coedge\") does not have the fields of its type in version 21500"),
                         "a tolerant coedge without its range");
}

// The facit's bodies in a new 2013 drawing (ASCII and binary): their SAB in ACDSDATA, read back
// with the extents of the same bodies written as 2010 with their SAT lines
TEST(acis_sab_spline_new_drawing) {
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
        int added = 0;
        for (const auto& f : kSabFacit) {
            if (std::string(f.label).rfind("3Dlines 21500", 0) != 0) continue;
            hdxf::AcisData d;
            std::string sat = f.sat;
            size_t a = 0;
            while (a < sat.size()) {
                size_t e = sat.find('\n', a);
                std::string line = sat.substr(a, e == std::string::npos ? std::string::npos : e - a);
                for (char& c : line) {
                    unsigned char u = static_cast<unsigned char>(c);
                    if (u > 32 && u < 159) c = static_cast<char>(159 - u);
                }
                d.lines.push_back(line);
                a = e == std::string::npos ? sat.size() : e + 1;
            }
            dwg.add_acis(hdxf::EntityType::Body, d);
            ++added;
        }
        hdxf_test::assert_eq(added, 5, "3Dlines' bodies");
        std::vector<uint8_t> out = dwg.save_to_memory(format);
        hdxf_test::assert_false(out.empty(), dwg.last_error());
        if (format == hdxf::DxfFormat::Ascii) dxf_structure::check_structure(out, "3Dlines' bodies as 2013");
        hdxf::Drawing back;
        hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
        size_t i = 0;
        for (const auto& e : back.entities()) {
            const hdxf::AcisData* d = acis_of(e);
            if (!d) continue;
            hdxf_test::assert_true(d->lines.empty() && !d->sab.empty(), "SAB, no SAT lines");
            const hdxf::AcisData& source = dwg.entities()[i].as_acis();
            std::vector<uint8_t> sab = encoded(hdxf::internal::acis_deciphered(source), "3Dlines");
            hdxf_test::assert_true(d->sab == sab, "the SAB of body " + std::to_string(i));
            ++i;
        }
        hdxf_test::assert_eq(static_cast<int>(i), 5, "five bodies read back");
        hdxf::Drawing as_2010 = dwg;
        as_2010.set_version(hdxf::DxfVersion::R2010);
        std::vector<uint8_t> old = as_2010.save_to_memory(format);
        hdxf::Drawing old_back;
        hdxf_test::assert_true(old_back.load_from_memory(old.data(), old.size()), old_back.last_error());
        for (const char* var : {"$EXTMIN", "$EXTMAX"}) {
            const hdxf::HeaderVar* x = back.find_header(var);
            const hdxf::HeaderVar* y = old_back.find_header(var);
            hdxf_test::assert_true(x && y && x->values == y->values, var);
        }
    }
}

// ---------------------------------------------------------------------------
// Step 5.7: the versions before 21200 (SAT 106, 400, 600, 700, 20800)
// ---------------------------------------------------------------------------

namespace {

// A face bounded by a closed ellipse edge as ACIS 1.06 saves it (as_flat.dxf's form): the header
// one line, sequence numbers, no history integer nor pattern pointer, the shell without a wire
// pointer, logicals as 0 and 1, no edge parameters nor convexity
std::string body_106() {
    return sat_text({"106 10 1 0",
                     "-0 body $-1 $1 $-1 $-1 #",
                     "-1 lump $-1 $-1 $2 $0 #",
                     "-2 shell $-1 $-1 $-1 $3 $1 #",
                     "-3 face $-1 $-1 $4 $2 $-1 $5 forward single #",
                     "-4 loop $-1 $-1 $6 $3 #",
                     "-5 plane-surface $-1 0 0 0 0 0 1 1 0 0 0 I I I I #",
                     "-6 coedge $-1 $6 $6 $-1 $7 0 $4 $-1 #",
                     "-7 edge $-1 $8 $8 $6 $9 forward #",
                     "-8 vertex $-1 $7 $10 #",
                     "-9 ellipse-curve $-1 0 0 0 0 0 1 2 0 0 1 I I #",
                     "-10 point $-1 0 2 0 #",
                     "End-of-ACIS-data"});
}

// A face bounded by one tolerant coedge on a plane as ACIS 6.0 saves it (speaker.dxf's form): the
// strings "n text", a tolerant edge with its tolerance, tolerant vertices with theirs, and the
// coedge's pcurve (the line v = 0 from u = 0 to 10 on the plane z = 0)
std::string tolerant_600() {
    return sat_text({"600 0 1 0",
                     "4 Test 8 ACIS 6.0 0 ",
                     "1 9.9999999999999995e-007 1e-010",
                     "body $-1 $1 $-1 $-1 #",
                     "lump $-1 $-1 $2 $0 #",
                     "shell $-1 $-1 $-1 $3 $-1 $1 #",
                     "face $-1 $-1 $4 $2 $-1 $5 forward single #",
                     "loop $-1 $-1 $6 $3 #",
                     "plane-surface $-1 0 0 0 0 0 1 1 0 0 forward_v I I I I #",
                     "tcoedge-coedge $-1 $6 $6 $-1 $7 forward $4 $11 0 10 #",
                     "tedge-edge $-1 $8 0 $9 10 $6 $10 forward 7 unknown 0.001 #",
                     "tvertex-vertex $-1 $7 $12 0.0005 #",
                     "tvertex-vertex $-1 $7 $13 0.0005 #",
                     "straight-curve $-1 0 0 0 1 0 0 I I #",
                     "pcurve $-1 0 forward { exppc nubs 1 open 2 0 1 10 1 0 0 10 0 0.001 plane 0 0 0 0 0 1 1 0 0 forward_v I I "
                     "I I } 0 0 #",
                     "point $-1 0 0 0 #",
                     "point $-1 10 0 0 #",
                     "End-of-ACIS-data"});
}

const std::string kFalse(1, '\x0b');
const std::string kTrue(1, '\x0a');

} // namespace

// SAT 106 as AutoCAD's chain makes it SAB (as_flat.dxf, B63): the asmheader in front (ODA's
// version 208.0.4.7009) and every pointer one on, an empty product, version and date and the unit
// 1, -1 and $-1 for the ENTITY data's tag and pattern, the shell's wire $-1, a logical from 0, the
// edge's parameters those of its vertex on the ellipse (π/2 and a turn on), its convexity
// "unknown", the vertex's integer 2
TEST(acis_sab_old_106) {
    std::vector<uint8_t> sab = encoded(body_106(), "SAT 106");
    std::string head = b_str("") + b_str("") + b_str("") + b_real(1.0) + b_real(1e-06) + b_real(1e-10);
    hdxf_test::assert_true(sab.size() > 31 && std::string(sab.begin() + 31, sab.begin() + 31 + static_cast<std::ptrdiff_t>(head.size())) == head,
                           "the head's strings and reals");
    hdxf_test::assert_true(has_bytes(sab, head + b_name("asmheader") + b_ptr(-1) + b_int(-1) + b_str("208.0.4.7009") + kEnd +
                                              b_name("body") + b_ptr(-1) + b_int(-1) + b_ptr(-1) + b_ptr(2) + b_ptr(-1) +
                                              b_ptr(-1) + kEnd),
                           "the asmheader in front, the body's pointers one on");
    hdxf_test::assert_true(has_bytes(sab, b_name("shell") + b_ptr(-1) + b_int(-1) + b_ptr(-1) + b_ptr(-1) + b_ptr(-1) + b_ptr(4) +
                                              b_ptr(-1) + b_ptr(2) + kEnd),
                           "the shell's wire");
    hdxf_test::assert_true(has_bytes(sab, b_name("plane-surface") + b_ptr(-1) + b_int(-1) + b_ptr(-1)), "a plane's ENTITY data");
    hdxf_test::assert_true(has_bytes(sab, b_name("coedge") + b_ptr(-1) + b_int(-1) + b_ptr(-1) + b_ptr(7) + b_ptr(7) + b_ptr(-1) +
                                              b_ptr(8) + kFalse + b_ptr(5) + b_int(0) + b_ptr(-1) + kEnd),
                           "the coedge's sense from 0");
    const double half = 1.5707963267948966;
    hdxf_test::assert_true(has_bytes(sab, b_name("edge") + b_ptr(-1) + b_int(-1) + b_ptr(-1) + b_ptr(9) + b_real(half) + b_ptr(9) +
                                              b_real(half + 6.283185307179586) + b_ptr(7) + b_ptr(10) + kFalse +
                                              b_str("unknown") + kEnd),
                           "the edge's parameters and convexity");
    hdxf_test::assert_true(has_bytes(sab, b_name("vertex") + b_ptr(-1) + b_int(-1) + b_ptr(-1) + b_ptr(8) + b_int(2) + b_ptr(11) + kEnd),
                           "the vertex's integer");
    same_bodies(body_106(), "SAT 106");
}

// Strings before 7.0 ("n text"), a straight edge's parameters along it and against it, the cone's
// u scale, and a SAT 106 exactcur's discontinuities by its knots' multiplicities (the chain lists
// the same of the first and second order)
TEST(acis_sab_old_fields) {
    std::string body = sat_text({"400 0 1 0", "16 Autodesk AutoCAD 12 ACIS 4.00 NT 0 ", "1 9.9999999999999995e-07 1e-10",
                                 "body $-1 $1 $-1 $-1 #", "lump $-1 $-1 $2 $0 #", "shell $-1 $-1 $-1 $3 $-1 $1 #",
                                 "face $-1 $-1 $4 $2 $-1 $5 forward double out #", "loop $-1 $-1 $6 $3 #",
                                 "cone-surface $-1 0 0 0 0 0 1 3 0 0 1 I I 0 1 forward I I I I #",
                                 "coedge $-1 $6 $6 $-1 $7 reversed $4 $-1 #", "edge $-1 $8 $9 $6 $10 reversed #",
                                 "vertex $-1 $7 $11 #", "vertex $-1 $7 $12 #", "straight-curve $-1 1 0 0 2 0 0 I I #",
                                 "point $-1 11 0 0 #", "point $-1 3 0 0 #", "End-of-ACIS-data"});
    std::vector<uint8_t> sab = encoded(body, "SAT 400");
    hdxf_test::assert_true(has_bytes(sab, b_str("Autodesk AutoCAD") + b_str("ACIS 4.00 NT") + b_str("")), "the head's strings");
    // From (11, 0, 0) to (3, 0, 0) on the line 1 + 2t: 5 and 1, negated against the line
    hdxf_test::assert_true(has_bytes(sab, b_ptr(9) + b_real(-5.0) + b_ptr(10) + b_real(-1.0) + b_ptr(7) + b_ptr(11) + kTrue +
                                              b_str("unknown") + kEnd),
                           "the edge's parameters against its line");
    hdxf_test::assert_true(has_bytes(sab, b_real(0.0) + b_real(1.0) + b_real(3.0) + kFalse), "the cone's u scale");

    std::string strings = sat_text({"600 0 1 0", "4 Test 8 ACIS 6.0 0 ", "1 9.9999999999999995e-007 1e-010",
                                    "body $-1 $1 $-1 $-1 #", "lump $-1 $-1 $2 $0 #", "shell $-1 $-1 $-1 $-1 $-1 $1 #",
                                    "End-of-ACIS-data"});
    hdxf_test::assert_true(has_bytes(encoded(strings, "SAT 600"), b_str("Test") + b_str("ACIS 6.0") + b_str("")),
                           "the head's strings without @");
    std::string edge = sat_text({"600 0 1 0", "4 Test 8 ACIS 6.0 0 ", "1 9.9999999999999995e-007 1e-010",
                                 "edge $-1 $-1 0 $-1 1 $-1 $-1 forward 7 unknown #", "End-of-ACIS-data"});
    hdxf_test::assert_true(has_bytes(encoded(edge, "SAT 600 edge"), b_real(1.0) + b_ptr(-1) + b_ptr(-1) + kFalse +
                                                                        b_str("unknown") + kEnd),
                           "a string \"7 unknown\"");

    // Degree 3: the knot 0.5 twice (second order), 0.75 once (third)
    std::string curve = sat_text({"106 3 1 0",
                                  "-0 intcurve-curve $-1 forward { exactcur nubs 3 open 4 0 3 0.5 2 0.75 1 1 3 0 0 0 1 0 0 2 0 0 "
                                  "3 0 0 4 0 0 5 0 0 6 0 0 0.001 null_surface null_surface nullbs nullbs } I I #",
                                  "End-of-ACIS-data"});
    std::vector<uint8_t> c = encoded(curve, "SAT 106 exactcur");
    hdxf_test::assert_true(has_bytes(c, b_word("exact_int_cur") + b_int(20900) + b_enum(0) + b_word("nubs")), "the version, full");
    hdxf_test::assert_true(has_bytes(c, b_word("nullbs") + b_word("nullbs") + kFalse + kFalse + b_int(0) + b_int(1) + b_real(0.5) +
                                            b_int(1) + b_real(0.75) + b_int(0) + kTrue + b_real(1.0) + kTrue + b_real(0.0) +
                                            b_enum(0) + b_enum(0) + "\x10"),
                           "the range, the discontinuities, the integer, the safe range, the extensions");
}

// A tolerant coedge, edge and vertices of SAT 600 (speaker.dxf): the coedge's 3D curve worked out as
// AutoCAD's chain does — its pcurve on its surface, a par_int_cur whose bs3 is the cubic Bézier
// through the curve's ends with their tangents (here the line itself, fit 0) — the edge's tolerance
// and 21301, the vertices' tolerance and -1 twice
TEST(acis_sab_old_tolerant) {
    std::vector<uint8_t> sab = encoded(tolerant_600(), "SAT 600");
    std::string line = b_word("nubs") + b_int(3) + b_enum(0) + b_int(2) + b_real(0.0) + b_int(3) + b_real(10.0) + b_int(3);
    hdxf_test::assert_true(has_bytes(sab, b_real(0.0) + b_real(10.0) + b_ptr(-1) + b_int(1) + b_word("intcurve") + kFalse + "\x0f" +
                                              b_word("par_int_cur") + b_int(23100) + b_enum(0) + line),
                           "the coedge's range, the curve pointer and its par_int_cur");
    AcisFile file;
    hdxf_test::assert_true(hdxf::internal::read_sab(sab, file), "the SAB read");
    const hdxf::internal::AcisRecord* coedge = nullptr;
    for (const auto& r : file.records) {
        if (r.type == "tcoedge-coedge") coedge = &r;
    }
    hdxf_test::assert_true(coedge != nullptr, "the tolerant coedge");
    if (!coedge) return;
    // After "nubs 3 open 2 (0 3) (10 3)": the four points on the line, the fit tolerance 0
    size_t at = 0;
    while (at < coedge->tokens.size() && !(coedge->tokens[at].type == hdxf::internal::AcisTokenType::Word &&
                                           coedge->tokens[at].text == "nubs")) {
        ++at;
    }
    at += 8;
    const double xs[4] = {0.0, 10.0 / 3.0, 20.0 / 3.0, 10.0};
    for (int k = 0; k < 4; ++k) {
        hdxf_test::assert_true(at + 3 * k + 2 < coedge->tokens.size(), "a point");
        if (at + 3 * k + 2 >= coedge->tokens.size()) return;
        hdxf_test::assert_true(std::fabs(coedge->tokens[at + 3 * k].number - xs[k]) <= 1e-12 &&
                                   std::fabs(coedge->tokens[at + 3 * k + 1].number) <= 1e-12 &&
                                   std::fabs(coedge->tokens[at + 3 * k + 2].number) <= 1e-12,
                               "the Bezier point " + std::to_string(k) + ": " +
                                   std::to_string(coedge->tokens[at + 3 * k].number) + " " +
                                   std::to_string(coedge->tokens[at + 3 * k + 1].number) + " " +
                                   std::to_string(coedge->tokens[at + 3 * k + 2].number));
    }
    hdxf_test::assert_eq(coedge->tokens[at + 12].number, 0.0, "the fit tolerance");
    hdxf_test::assert_true(has_bytes(sab, b_real(0.0) + b_word("plane")), "the pcurve's surface");
    hdxf_test::assert_true(has_bytes(sab, b_word("null_surface") + b_word("nubs") + b_int(1) + b_enum(0) + b_int(2) + b_real(0.0) +
                                              b_int(1) + b_real(10.0) + b_int(1) + b_real(0.0) + b_real(0.0) + b_real(10.0) +
                                              b_real(0.0) + b_word("nullbs") + kFalse + kFalse + b_int(0) + b_int(0) +
                                              b_int(0) + b_int(0) + kTrue + kFalse + "\x10" + kFalse + kFalse + kEnd),
                           "the bs2 curve and the rest of the par_int_cur");
    hdxf_test::assert_true(has_bytes(sab, b_str("unknown") + b_real(0.001) + b_int(21301) + kEnd), "the tolerant edge");
    hdxf_test::assert_true(has_bytes(sab, b_ptr(13) + b_real(0.0005) + b_real(-1.0) + b_real(-1.0) + kEnd), "the tolerant vertex");
    // The par_int_cur is a definition: a "ref" after it is to the SAB's numbers
    std::string shared = tolerant_600();
    shared.replace(shared.find("plane-surface $-1 0 0 0 0 0 1 1 0 0 forward_v I I I I"), 53,
                   "spline-surface $-1 forward { exactsur full nubs 1 1 open open none none 2 2 0 1 10 1 0 1 10 1 "
                   "0 0 0 10 0 0 0 10 0 10 10 0 0 0 0 0 0 0 0 F 0 F 10 F 0 F 10 } I I I I");
    shared.replace(shared.find("plane 0 0 0 0 0 1 1 0 0 forward_v I I I I"), 41, "spline forward { ref 0 } I I I I");
    std::vector<uint8_t> s = encoded(shared, "SAT 600 on a spline surface");
    // The face's surface the SAB's definition 0, the par_int_cur 1, its copy of the surface 2; the
    // pcurve's reference to the SAT's 0 is to the SAB's 0
    hdxf_test::assert_true(has_bytes(s, b_word("spline") + kFalse + "\x0f" + b_word("exact_spl_sur")), "the copy");
    hdxf_test::assert_true(has_bytes(s, b_name("pcurve") + b_ptr(-1) + b_int(-1) + b_ptr(-1) + b_int(0) + kFalse + "\x0f" +
                                            b_word("exp_par_cur")),
                           "the pcurve");
    hdxf_test::assert_true(has_bytes(s, b_word("spline") + kFalse + "\x0f" + b_word("ref") + b_int(0) + "\x10"), "the pcurve's ref");
}

// The asmheader put in front of a SAT that has none: its version from the header's "ASM …"
// (AutoCAD's core reads both forms with the same mass properties, ACadSharp's D65 so made; B63)
TEST(acis_sab_old_asmheader) {
    std::string body = wire_body("out");
    size_t a = body.find("asmheader");
    std::string without = body.substr(0, a) + body.substr(body.find('\n', a) + 1);
    // Every pointer one less
    std::string moved;
    for (size_t i = 0; i < without.size(); ++i) {
        if (without[i] == '$' && i + 1 < without.size() && std::isdigit(static_cast<unsigned char>(without[i + 1]))) {
            size_t e = i + 1;
            while (e < without.size() && std::isdigit(static_cast<unsigned char>(without[e]))) ++e;
            moved += "$" + std::to_string(std::atoi(without.substr(i + 1, e - i - 1).c_str()) - 1);
            i = e - 1;
            continue;
        }
        moved += without[i];
    }
    std::vector<uint8_t> ours = encoded(moved, "no asmheader");
    std::vector<uint8_t> with = encoded(body, "with an asmheader");
    hdxf_test::assert_true(ours == with, "the same SAB");
}

// The reference's form of a SAT line (a 1 and 3s after it) written by a program that counts the
// characters as they stand in the file — a caret escape two (speaker.dxf's "^I", read before as two
// lines: AutoCAD's core reads the file so too, error 75004 — the same file in AutoCAD's form it
// reads with the volume 13607.7203)
TEST(acis_sat_lines_reference_carets) {
    hdxf::Drawing built(hdxf::DxfVersion::R2010);
    hdxf::AcisData acis;
    acis.lines = {"short", std::string(1, '\t') + std::string(299, 'a'), "end"};
    built.add_acis(hdxf::EntityType::Region, acis);
    std::vector<uint8_t> b = built.save_to_memory();
    std::string text(b.begin(), b.end());
    std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string autocad_form = "  3" + eol + "^I" + std::string(254, 'a') + eol + "  1" + eol + std::string(45, 'a') + eol;
    std::string reference_form = "  1" + eol + "^I" + std::string(253, 'a') + eol + "  3" + eol + std::string(46, 'a') + eol;
    size_t at = text.find(autocad_form);
    hdxf_test::assert_true(at != std::string::npos, "AutoCAD's form in the file");
    if (at == std::string::npos) return;
    text.replace(at, autocad_form.size(), reference_form);
    hdxf::Drawing reference;
    hdxf_test::assert_true(reference.load_from_memory(reinterpret_cast<const uint8_t*>(text.data()), text.size()),
                           reference.last_error());
    hdxf_test::assert_true(reference.entities().at(0).as_acis().lines == acis.lines, "one line of 300 characters");
}

// ACadSharp's 2000 and 2004 bodies (SAT 400, 20800) in a new 2013 drawing, ASCII and binary: their
// SAB in ACDSDATA, read back; the extents those of the same drawing as 2010 with the SAT lines
TEST(acis_sab_old_new_drawing) {
    for (const char* name : {"acadsharp/sample_AC1015_ascii.dxf", "acadsharp/sample_AC1018_ascii.dxf"}) {
        if (!has_files({name})) return;
        hdxf::Drawing src = load(name);
        for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
            hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
            std::vector<hdxf::AcisData> sources;
            for (const auto& e : src.entities()) {
                const hdxf::AcisData* d = acis_of(e);
                if (!d || d->lines.empty()) continue;
                hdxf::AcisData copy = *d;
                copy.history_handle.clear();
                dwg.add_acis(e.type, copy);
                sources.push_back(copy);
            }
            hdxf_test::assert_eq(static_cast<int>(sources.size()), 3, std::string(name) + ": the bodies");
            std::vector<uint8_t> out = dwg.save_to_memory(format);
            hdxf_test::assert_false(out.empty(), dwg.last_error());
            if (format == hdxf::DxfFormat::Ascii) dxf_structure::check_structure(out, std::string(name) + " as 2013");
            hdxf::Drawing back;
            hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
            size_t i = 0;
            for (const auto& e : back.entities()) {
                const hdxf::AcisData* d = acis_of(e);
                if (!d) continue;
                hdxf_test::assert_true(i < sources.size() && d->lines.empty() &&
                                           d->sab == encoded(hdxf::internal::acis_deciphered(sources[i]), name),
                                       std::string(name) + ": the SAB of body " + std::to_string(i));
                ++i;
            }
            hdxf::Drawing as_2010 = dwg;
            as_2010.set_version(hdxf::DxfVersion::R2010);
            std::vector<uint8_t> old = as_2010.save_to_memory(format);
            hdxf::Drawing old_back;
            hdxf_test::assert_true(old_back.load_from_memory(old.data(), old.size()), old_back.last_error());
            for (const char* var : {"$EXTMIN", "$EXTMAX"}) {
                const hdxf::HeaderVar* x = back.find_header(var);
                const hdxf::HeaderVar* y = old_back.find_header(var);
                hdxf_test::assert_true(x && y && x->values == y->values, std::string(name) + " " + var);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Step 6.8 (phase 6, plan B80/B81): the SAB 22300 of DXF 2018 — "ASM BinaryFile4", the head
// (22300, 0, 2, 4), the records AutoCAD's 21800 has. Facit: AutoCAD's own 22300 SAB of the bodies
// (ACadSharp's and LibreDWG's 2018 files against their 2007/2010 SAT), ODA File Converter's
// ACAD2018 DXF of the DWG AutoCAD's chain made (the same DWG as the 21800 facit), and every body of
// the 21800 facit through both formats.
// ---------------------------------------------------------------------------

namespace {

constexpr int k21800 = hdxf::internal::sab_format_acis;
constexpr int k22300 = hdxf::internal::sab_format_asm;

std::string sab_error_as(const std::string& sat, int format) {
    std::vector<uint8_t> out;
    std::string error;
    hdxf_test::assert_false(hdxf::internal::sab_from_sat(sat, out, error, format), "no SAB");
    hdxf_test::assert_true(out.empty(), "nothing made");
    return error;
}

int32_t head_version(const std::vector<uint8_t>& sab) {
    int32_t v = 0;
    if (sab.size() >= 19) std::memcpy(&v, sab.data() + 15, 4);
    return v;
}

std::string signature(const std::vector<uint8_t>& sab) {
    return sab.size() >= 15 ? std::string(sab.begin(), sab.begin() + 15) : std::string();
}

// The stream after the head (15 bytes of signature, 16 of integers) is the same, the head is each format's
void same_stream_but_head(const std::vector<uint8_t>& a22300, const std::vector<uint8_t>& a21800, const std::string& what) {
    hdxf_test::assert_true(a22300.size() > 31 && a21800.size() > 31, what + ": SAB");
    if (a22300.size() <= 31 || a21800.size() <= 31) return;
    hdxf_test::assert_eq(signature(a22300), std::string("ASM BinaryFile4"), what + ": 22300's signature");
    hdxf_test::assert_eq(signature(a21800), std::string("ACIS BinaryFile"), what + ": 21800's signature");
    int32_t head22[4], head21[4];
    std::memcpy(head22, a22300.data() + 15, 16);
    std::memcpy(head21, a21800.data() + 15, 16);
    hdxf_test::assert_true(head22[0] == 22300 && head22[1] == 0 && head22[2] == 2 && head22[3] == 4, what + ": (22300, 0, 2, 4)");
    hdxf_test::assert_true(head21[0] == 21800 && head21[1] == 0 && head21[2] == 2 && head21[3] == 12, what + ": (21800, 0, 2, 12)");
    hdxf_test::assert_eq(static_cast<int>(a22300.size()), static_cast<int>(a21800.size()), what + ": the same length");
    hdxf_test::assert_true(a22300.size() == a21800.size() && std::equal(a22300.begin() + 31, a22300.end(), a21800.begin() + 31),
                           what + ": the same stream after the head");
}

} // namespace

// The head, the end and the format that is asked for: 22300 is "ASM BinaryFile4" with (22300, 0, 2, 4),
// the stream after it the one 21800 has; a format that is neither is an error and writes nothing
TEST(acis_sab_22300_head) {
    std::vector<uint8_t> a = encoded(wire_body("out"), "21800");
    std::vector<uint8_t> b = encoded(wire_body("out"), "22300", k22300);
    same_stream_but_head(b, a, "wire");
    hdxf_test::assert_eq(head_version(b), 22300, "the version");
    const uint8_t head[] = {0x1C, 0x57, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 4, 0, 0, 0};
    hdxf_test::assert_true(b.size() > 31 && std::memcmp(b.data() + 15, head, 16) == 0, "22300 is 0x571C");
    const uint8_t end[] = {14, 3, 'E', 'n', 'd', 14, 2, 'o', 'f', 14, 3, 'A', 'S', 'M', 13, 4, 'd', 'a', 't', 'a'};
    hdxf_test::assert_true(b.size() > sizeof end && std::memcmp(b.data() + b.size() - sizeof end, end, sizeof end) == 0,
                           "End-of-ASM-data");
    // 21800 is the default
    std::vector<uint8_t> out;
    std::string error;
    hdxf_test::assert_true(hdxf::internal::sab_from_sat(wire_body("out"), out, error), error);
    hdxf_test::assert_true(out == a, "21800 without a format");
    for (int format : {0, 21500, 22301, 99999}) {
        hdxf_test::assert_eq(sab_error_as(wire_body("out"), format),
                             "the SAB format " + std::to_string(format) + " is not 21800 or 22300", "format");
    }
    // The format of a drawing of each version
    hdxf_test::assert_eq(hdxf::internal::sab_format_for(hdxf::DxfVersion::R2013), 21800, "2013");
    hdxf_test::assert_eq(hdxf::internal::sab_format_for(hdxf::DxfVersion::R2010), 21800, "2010");
    hdxf_test::assert_eq(hdxf::internal::sab_format_for(hdxf::DxfVersion::R2018), 22300, "2018");
}

// The errors name the DXF version the SAB is for: 21800 DXF 2013, 22300 DXF 2018
TEST(acis_sab_22300_errors) {
    std::string body = wire_body("out");
    auto with_version = [&](const char* v) { return std::string(v) + body.substr(5); };
    // A SAT of a version after 21800 does not exist in a DXF file (22300 is the SAB's)
    hdxf_test::assert_eq(sab_error_as(with_version("22300"), k22300),
                         std::string("ACIS data of version 22300 cannot be written as DXF 2018"), "a later version");
    hdxf_test::assert_eq(sab_error_as(with_version("99"), k22300),
                         std::string("ACIS data of version 99 cannot be written as DXF 2018"), "no ACIS version");
    hdxf_test::assert_eq(sab_error_as(with_version("22300"), k21800),
                         std::string("ACIS data of version 22300 cannot be written as DXF 2013"), "a later version as 2013");
    std::string unknown = body;
    unknown.replace(unknown.find("straight-curve"), 14, "helix-curve");
    hdxf_test::assert_eq(sab_error_as(unknown, k22300), std::string("the ACIS record \"helix-curve\" cannot be written as DXF 2018"),
                         "an unknown record type");
    std::string misnamed = body;
    misnamed.replace(misnamed.find("asmheader"), 9, "xasmheader");
    hdxf_test::assert_eq(sab_error_as(misnamed, k22300), std::string("the ACIS record \"xasmheader\" cannot be written as DXF 2018"),
                         "an unknown first record");
    std::string spline = body;
    spline.replace(spline.find("straight-curve"), 14, "intcurve-curve");
    hdxf_test::assert_eq(sab_error_as(spline, k22300),
                         std::string("the ACIS record 9 (\"intcurve-curve\") does not have the fields of its type in version 21500"),
                         "fields of another type");
    hdxf_test::assert_eq(sab_error_as(attributed_body("ATTRIB_CUSTOM-attrib $-1 -1 $-1 $-1 $1 @9 other_def 1"), k22300),
                         std::string("the ACIS attribute definition \"other_def\" (record 2, \"ATTRIB_CUSTOM-attrib\") "
                                     "cannot be written as DXF 2018"),
                         "an unknown attribute definition");
    hdxf_test::assert_eq(sab_error_as(attributed_body("name_attrib-gen-attrib $-1 -1 $-1 $-1 $1 split lose apply copy @1 x"), k22300),
                         std::string("the ACIS word \"split\" (record 2, \"name_attrib-gen-attrib\") cannot be written as DXF 2018"),
                         "an unknown word");
    // A SAT cut off in a record: not SAT, or a last record without its end, never a SAB of part of the body
    size_t edge = body.find("edge $-1 -1 $-1 $7 0 $8");
    hdxf_test::assert_true(edge != std::string::npos, "the edge");
    hdxf_test::assert_eq(sab_error_as(body.substr(0, 10), k22300), std::string("the ACIS data cannot be read as SAT"), "cut in the head");
    size_t point = body.find("point $-1 -1 $-1 10 0 0");
    hdxf_test::assert_true(point != std::string::npos, "the point");
    for (size_t cut : {edge + 12, edge + 20, point + 15, point + 20}) {
        for (int format : {k21800, k22300}) {
            hdxf_test::assert_eq(sab_error_as(body.substr(0, cut), format),
                                 std::string("the ACIS data is cut off: its last record has no end"), "cut at " + std::to_string(cut));
        }
    }
    // The SAT of AutoCAD's 2000 and 2004 has no end record: it ends with its last record's "#"
    std::string no_end = body.substr(0, body.find("End-of-ASM-data"));
    std::vector<uint8_t> made = encoded(no_end, "no end record", k22300);
    hdxf_test::assert_true(made.size() > 31, "made");
}

// AutoCAD's SAT 21200 (2007) and 21500 (2010) of a drawing against its SAB 22300 (2018) of the same
// drawing: byte for byte but the date (ACadSharp's D65, D69, D6A in ASCII and binary; LibreDWG's
// 176, 2E1, 37D). One is AutoCAD's own exception: D69 of the ASCII 2018 file is 21800, "ACIS
// BinaryFile" (AutoCAD kept the SAB it had); our 21800 is that one, and our 22300 of it the same
// stream with 22300's head
TEST(acis_sab_22300_autocad_pairs) {
    struct Set {
        const char* older[2];
        const char* newer;
        std::vector<std::string> handles;
    };
    const Set sets[] = {
        {{"acadsharp/sample_AC1021_ascii.dxf", "acadsharp/sample_AC1024_ascii.dxf"}, "acadsharp/sample_AC1032_ascii.dxf", {"D65", "D69", "D6A"}},
        {{"acadsharp/sample_AC1021_binary.dxf", "acadsharp/sample_AC1024_binary.dxf"}, "acadsharp/sample_AC1032_binary.dxf", {}},
        {{"libre/example_2007.dxf", "libre/example_2010.dxf"}, "libre/example_2018.dxf", {"176", "2E1", "37D"}}};
    int in_22300 = 0, in_21800 = 0;
    for (const auto& set : sets) {
        if (!has_files({set.older[0], set.older[1], set.newer})) return;
        hdxf::Drawing r2018 = load(set.newer);
        // The handles of the binary files: the bodies of the 2018 file
        std::vector<std::string> handles = set.handles;
        if (handles.empty()) {
            for (const auto& e : r2018.entities()) {
                if (acis_of(e)) handles.push_back(e.props.handle);
            }
            hdxf_test::assert_eq(static_cast<int>(handles.size()), 3, std::string(set.newer) + ": the bodies");
        }
        for (const char* older_file : set.older) {
            hdxf::Drawing older = load(older_file);
            for (const auto& h : handles) {
                std::string what = std::string(older_file) + " " + h + " → " + set.newer;
                const hdxf::AcisData* sat = body(older, h);
                const hdxf::AcisData* sab = body(r2018, h);
                hdxf_test::assert_true(sat && sab && !sat->lines.empty() && !sab->sab.empty(), what + ": the pair");
                if (!sat || !sab) continue;
                std::string text = hdxf::internal::acis_deciphered(*sat);
                if (head_version(sab->sab) == 22300) {
                    std::vector<uint8_t> ours = encoded(text, what, k22300);
                    hdxf_test::assert_eq(date_of(ours), std::string(), what + ": the SAT's empty date");
                    hdxf_test::assert_false(date_of(sab->sab).empty(), what + ": AutoCAD's date");
                    same_bytes(without_date(ours), without_date(sab->sab), what);
                    ++in_22300;
                } else {
                    // AutoCAD's own SAB 21800 inside a 2018 file
                    hdxf_test::assert_eq(head_version(sab->sab), 21800, what + ": AutoCAD's other format");
                    std::vector<uint8_t> ours = encoded(text, what, k21800);
                    same_bytes(without_date(ours), without_date(sab->sab), what + " (21800)");
                    same_stream_but_head(encoded(text, what, k22300), ours, what);
                    ++in_21800;
                }
            }
        }
    }
    // 2 ascii pairs × 2 in 22300 (+ the exception), 2 binary × 3, 2 libre × 3
    hdxf_test::assert_eq(in_22300, 2 * 2 + 2 * 3 + 2 * 3, "the bodies in 22300");
    hdxf_test::assert_eq(in_21800, 2 * 1, "the body AutoCAD kept in 21800");
}

// AutoCAD's SAB 22300 of the bodies of its 2018 files is its SAB 21800 of the same bodies in its 2013 files:
// the head and the date are all that differs (LibreDWG's three bodies, ACadSharp's three in the binary files)
TEST(acis_sab_22300_is_21800_in_autocad_files) {
    const char* const pairs[][2] = {{"libre/example_2013.dxf", "libre/example_2018.dxf"},
                                    {"acadsharp/sample_AC1027_binary.dxf", "acadsharp/sample_AC1032_binary.dxf"}};
    int compared = 0;
    for (const auto& p : pairs) {
        if (!has_files({p[0], p[1]})) return;
        hdxf::Drawing r2013 = load(p[0]);
        hdxf::Drawing r2018 = load(p[1]);
        for (const auto& e : r2018.entities()) {
            const hdxf::AcisData* n = acis_of(e);
            const hdxf::AcisData* o = body(r2013, e.props.handle);
            if (!n || !o || n->sab.empty() || o->sab.empty()) continue;
            std::string what = std::string(p[1]) + " " + e.props.handle;
            hdxf_test::assert_eq(head_version(n->sab), 22300, what);
            hdxf_test::assert_eq(head_version(o->sab), 21800, what);
            // The streams after the head: the records, the units, the end; the date apart
            std::vector<uint8_t> headed = n->sab;
            std::copy(o->sab.begin(), o->sab.begin() + 31, headed.begin());
            same_bytes(without_date(headed), without_date(o->sab), what + " (the head apart)");
            ++compared;
        }
    }
    hdxf_test::assert_eq(compared, 6, "the bodies");
}

// Every body of the 21800 facit (SAT 106 … 21800: AutoCAD's own SAB and its chain's) through both
// formats: the same stream after the head; read back, 22300's SAB gives the SAT's records, bodies and
// extents
TEST(acis_sab_22300_same_body) {
    int bodies = 0;
    for (const auto& f : kSabFacit) {
        std::vector<uint8_t> a = encoded(f.sat, f.label, k21800);
        std::vector<uint8_t> b = encoded(f.sat, f.label, k22300);
        if (a.empty() || b.empty()) continue;
        same_stream_but_head(b, a, f.label);
        same_bodies(f.sat, f.label, k22300);
        ++bodies;
    }
    hdxf_test::assert_eq(bodies, 25 + 23, "the facit's bodies");
}

// A 64-bit integer is exact in the SAB (tag 23) in both formats: above 2^53, at the limits of int64,
// negative — from the SAT's text to the bytes, and from the SAB's bytes to the token
TEST(acis_sab_22300_int64) {
    struct Case {
        const char* text;
        int64_t value;
    };
    const Case cases[] = {{"9007199254740993", 9007199254740993LL},   // 2^53 + 1
                          {"-9007199254740993", -9007199254740993LL},
                          {"9223372036854775807", INT64_MAX},
                          {"-9223372036854775807", -INT64_MAX},
                          {"4611686018427387905", 4611686018427387905LL},
                          {"1788555391449218", 1788555391449218LL}};
    for (const auto& c : cases) {
        std::string sat = attributed_body(std::string("int64_attrib-name_attrib-gen-attrib $-1 -1 $-1 $-1 $1 "
                                                      "copy keep_all ignore copy @9 stamp_uid ") + c.text);
        std::string value(1, '\x17');
        for (int i = 0; i < 8; ++i) value += static_cast<char>((static_cast<uint64_t>(c.value) >> (8 * i)) & 0xFF);
        for (int format : {k21800, k22300}) {
            std::vector<uint8_t> sab = encoded(sat, c.text, format);
            hdxf_test::assert_true(has_bytes(sab, b_name("int64_attrib-name_attrib-gen-attrib") + b_attrib_head() + b_enum(2) +
                                                      b_enum(4) + b_enum(1) + b_enum(2) + b_str("stamp_uid") + value + kEnd),
                                   std::string(c.text) + ": tag 23 and the value's eight bytes");
            // Read back: the token keeps every digit
            AcisFile file;
            hdxf_test::assert_true(hdxf::internal::read_sab(sab, file), std::string(c.text) + ": read");
            bool found = false;
            for (const auto& r : file.records) {
                if (r.type != "int64_attrib-name_attrib-gen-attrib") continue;
                found = !r.tokens.empty() && r.tokens.back().text == c.text;
            }
            hdxf_test::assert_true(found, std::string(c.text) + ": the token's digits");
        }
    }
    // Not an integer, and an integer beyond int64: no SAB
    for (const char* text : {"1.5", "9223372036854775808"}) {
        hdxf_test::assert_eq(
            sab_error_as(attributed_body(std::string("int64_attrib-name_attrib-gen-attrib $-1 -1 $-1 $-1 $1 copy keep_all ignore copy @1 x ") + text),
                         k22300),
            std::string("the ACIS record 2 (\"int64_attrib-name_attrib-gen-attrib\") does not have the fields of its type in version 21500"),
            text);
    }
}

// A SAB that is already there is written as it is, whatever its format: AutoCAD's 22300 and 21800, a
// higher version and a stream the encoder would never make — a body without SAT lines goes through no
// encoder (B80). Written as 2013, ASCII and binary, read back byte for byte
TEST(acis_sab_existing_is_kept) {
    if (!has_files({"libre/example_2013.dxf", "libre/example_2018.dxf"})) return;
    hdxf::Drawing r2013 = load("libre/example_2013.dxf");
    hdxf::Drawing r2018 = load("libre/example_2018.dxf");
    std::vector<std::vector<uint8_t>> sabs;
    for (const auto& e : r2018.entities()) {
        if (const hdxf::AcisData* d = acis_of(e)) sabs.push_back(d->sab);
    }
    hdxf_test::assert_eq(static_cast<int>(sabs.size()), 3, "AutoCAD's 22300");
    for (const auto& e : r2013.entities()) {
        if (const hdxf::AcisData* d = acis_of(e)) sabs.push_back(d->sab);
    }
    hdxf_test::assert_eq(static_cast<int>(sabs.size()), 6, "and 21800");
    // A format after 22300, and a stream the encoder does not make (a record type of its own)
    std::vector<uint8_t> higher = sabs[0];
    int32_t later = 22400;
    std::memcpy(higher.data() + 15, &later, 4);
    sabs.push_back(higher);
    std::vector<uint8_t> unknown = sabs[1];
    const uint8_t extra[] = {13, 5, 'x', 'q', 'z', 'y', 'w', 17};
    unknown.insert(unknown.end() - 20, std::begin(extra), std::end(extra));
    sabs.push_back(unknown);
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
        for (const auto& s : sabs) {
            hdxf::AcisData d;
            d.sab = s;
            dwg.add_acis(hdxf::EntityType::Solid3D, d);
        }
        std::vector<uint8_t> out = dwg.save_to_memory(format);
        hdxf_test::assert_false(out.empty(), dwg.last_error());
        hdxf::Drawing back;
        hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
        size_t i = 0;
        for (const auto& e : back.entities()) {
            const hdxf::AcisData* d = acis_of(e);
            if (!d) continue;
            hdxf_test::assert_true(i < sabs.size() && d->sab == sabs[i], "the SAB of body " + std::to_string(i) + " unchanged");
            ++i;
        }
        hdxf_test::assert_eq(static_cast<int>(i), static_cast<int>(sabs.size()), "the bodies");
    }
}

// A string over 255 bytes is tag 8 (a 2-byte length), over 65535 tag 9 (a 4-byte length), in both formats
TEST(acis_sab_22300_long_strings) {
    for (size_t length : {size_t(255), size_t(256), size_t(300), size_t(65535), size_t(65536), size_t(70000)}) {
        std::string text(length, 'q');
        std::string sat = attributed_body("name_attrib-gen-attrib $-1 -1 $-1 $-1 $1 lose lose apply copy @" + std::to_string(length) + " " + text);
        std::string tagged;
        if (length < 256) {
            tagged = std::string("\x07") + static_cast<char>(length);
        } else if (length < 65536) {
            tagged = std::string("\x08") + static_cast<char>(length & 0xFF) + static_cast<char>(length >> 8);
        } else {
            tagged = std::string("\x09") + static_cast<char>(length & 0xFF) + static_cast<char>((length >> 8) & 0xFF) +
                     static_cast<char>((length >> 16) & 0xFF) + static_cast<char>(length >> 24);
        }
        for (int format : {k21800, k22300}) {
            std::vector<uint8_t> sab = encoded(sat, std::to_string(length), format);
            hdxf_test::assert_true(has_bytes(sab, b_enum(2) + b_enum(2) + tagged + text + kEnd),
                                   std::to_string(length) + ": the string's tag and length");
            AcisFile file;
            hdxf_test::assert_true(hdxf::internal::read_sab(sab, file), std::to_string(length) + ": read");
            bool found = false;
            for (const auto& r : file.records) {
                if (r.type != "name_attrib-gen-attrib") continue;
                for (const auto& t : r.tokens) found = found || (t.type == hdxf::internal::AcisTokenType::String && t.text == text);
            }
            hdxf_test::assert_true(found, std::to_string(length) + ": the string back");
        }
    }
}

// A 22300 SAB cut off anywhere is read without a crash (records before the cut or nothing), and a
// body in a read file with a SAB that is cut off or has a wrong 94 is written back as it was read
TEST(acis_sab_22300_cut_off) {
    if (!has_files({"libre/example_2018.dxf", "acadsharp/sample_AC1027_ascii.dxf"})) return;
    hdxf::Drawing r2018 = load("libre/example_2018.dxf");
    const hdxf::AcisData* region = body(r2018, "176");
    hdxf_test::assert_true(region && region->sab.size() > 100, "AutoCAD's 22300 SAB of 176");
    if (!region) return;
    AcisFile whole;
    hdxf_test::assert_true(hdxf::internal::read_sab(region->sab, whole), "whole");
    for (size_t cut = 0; cut < region->sab.size(); cut += 97) {
        std::vector<uint8_t> part(region->sab.begin(), region->sab.begin() + static_cast<std::ptrdiff_t>(cut));
        AcisFile file;
        bool ok = hdxf::internal::read_sab(part, file);
        hdxf_test::assert_true(!ok || file.records.size() <= whole.records.size(), "records of a cut at " + std::to_string(cut));
        // the extents of what was read: no crash
        Extents e;
        hdxf::internal::acis_extents(hdxf::internal::acis_bodies(file), hdxf::internal::AcisMap{}, e);
    }

    // A 2013 file whose body has a wrong 94: written back as read
    std::vector<uint8_t> original = read_bytes(external("acadsharp/sample_AC1027_ascii.dxf"));
    std::string text(original.begin(), original.end());
    {
        hdxf::Drawing plain;
        hdxf_test::assert_true(plain.load_from_memory(original.data(), original.size()), plain.last_error());
        std::vector<uint8_t> again = plain.save_to_memory(hdxf::DxfFormat::Ascii);
        hdxf_test::assert_true(again == original, "the file unchanged: byte for byte");
    }
    size_t at = text.find("ASM_Data\r\n");
    if (at == std::string::npos) at = text.find("ASM_Data\n");
    // the record's 94 after the schema's: the second ASM_Data
    at = text.find("ASM_Data", text.find("ASM_Data", at + 1));
    hdxf_test::assert_true(at != std::string::npos, "an ASM_Data record");
    size_t count = text.find(" 94", at);
    hdxf_test::assert_true(count != std::string::npos, "its 94");
    if (count == std::string::npos) return;
    size_t line = text.find('\n', count) + 1;
    size_t line_end = text.find('\n', line);
    std::string wrong = text;
    std::string width(line_end - line - (text[line_end - 1] == '\r' ? 1 : 0), ' ');
    wrong.replace(line, width.size(), width.substr(0, width.size() - 1) + "7");
    hdxf_test::assert_true(wrong != text, "a 94 that is not the length");
    hdxf::Drawing odd;
    hdxf_test::assert_true(odd.load_from_memory(reinterpret_cast<const uint8_t*>(wrong.data()), wrong.size()), odd.last_error());
    std::vector<uint8_t> back = odd.save_to_memory(hdxf::DxfFormat::Ascii);
    hdxf_test::assert_true(std::string(back.begin(), back.end()) == wrong, "a wrong 94 written back as read");
}

// "#" separates records as a blank does: a SAT ending "…#End-of-ACIS-data" (no blank after the "#") is whole,
// whichever the end record and with or without a "#" after it; a drawing with it is saved as 2013
TEST(acis_sab_end_record_after_hash) {
    std::string body = wire_body("out");
    const std::string tail = "\nEnd-of-ASM-data #\n";
    size_t at = body.find(tail);
    hdxf_test::assert_true(at != std::string::npos, "the end record");
    if (at == std::string::npos) return;
    std::string head = body.substr(0, at);
    std::vector<uint8_t> expected21 = encoded(body, "newline", k21800);
    std::vector<uint8_t> expected22 = encoded(body, "newline", k22300);
    for (const char* end : {"End-of-ASM-data", "End-of-ASM-data #", "End-of-ACIS-data", "End-of-ACIS-data #"}) {
        std::string sat = head + end;
        hdxf_test::assert_true(encoded(sat, end, k21800) == expected21, std::string(end) + ": 21800");
        hdxf_test::assert_true(encoded(sat, end, k22300) == expected22, std::string(end) + ": 22300");
        hdxf_test::assert_true(encoded(sat + "\n", end, k22300) == expected22, std::string(end) + ": with a newline");
    }
    hdxf::AcisData acis;
    std::string line;
    std::string sat = head + "End-of-ACIS-data";
    for (char c : sat) {
        if (c == '\n') {
            acis.lines.push_back(line);
            line.clear();
            continue;
        }
        unsigned char u = static_cast<unsigned char>(c);
        line += (u > 32 && u < 159) ? static_cast<char>(159 - u) : c;
    }
    acis.lines.push_back(line);
    hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
    dwg.add_acis(hdxf::EntityType::Solid3D, acis);
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
}
