#include "acis_sab.hpp"
#include "acis.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>

// SAT → SAB 21800 (plan phase 5, B62/B63) and 22300 (phase 6, step 6.8, B80/B81). The SAT is read into records (read_sat()); each
// record is written by its type's field list in the SAT's version. SAT does not say whether a
// number is an integer or a real, a position or a vector, nor what a word means: the field
// list does. The lists are AutoCAD's, record type by record type: its SAT 21200 and 21500
// (ACadSharp's sample_AC1021/AC1024, LibreDWG's example_2007/2010) against its SAB 21800 of the
// same bodies (sample_AC1027, example_2013), and AutoCAD's chain (DXFIN → -WBLOCK → ODA
// ACAD2013 DXF, B66) for the record types those bodies do not have (Aspose's conic_pyramid,
// endstop_ztop, test3D; a wire). The spline geometry and the other attributes (step 5.6):
// AutoCAD's SAB 21800 of jscad's 3Dlines, ahmeed, the f360 files and apollo against the SAT
// 21500 ODA makes of them, and the chain's SAB of the SAT 21200 and 21500 ODA makes of the chain's
// own DWG (where ODA's SAT loses something: the chain's SAB of that SAT). The older versions
// (step 5.7: SAT 106, 400, 600, 700, 20800 — AutoCAD R13/R14, 2000 and 2004, ODA's and ezdxf's
// 7.0, other programs' 6.0): the chain's SAB of ACadSharp's 2000 and 2004 bodies, LibreDWG's
// Surface_2004, ezdxf's 3dsolids, the 7.0 bodies of test_acis.cpp and of the local files as_flat
// (106), speaker (600), capp, kolja, osgeo and safecast — what the version does not save, the
// field lists give as the chain has it: the asmheader in front, the ENTITY data's tag and pattern,
// the edges' parameters and convexity, the vertices' integer, the subtypes' names and their newer
// fields, a tolerant coedge's 3D curve.
//
// SAB (ACIS 7.0's "SAT Save File Format" and AutoCAD's files): after the head each value has a
// tag byte, little-endian — 4 integer, 6 real, 7/8/9 string with a 1/2/4-byte length,
// 10 true, 11 false, 12 pointer, 13 identifier (a record name's last part, or a word of the
// data), 14 a part before it, 15/16 a subtype's start and end, 17 the record's end, 18 text,
// 19 position, 20 vector (three reals), 21 enum, 23 64-bit integer. AutoCAD never writes 2, 3,
// 5 (smaller integers, a float).

namespace hdxf {
namespace internal {

namespace {

// A record type's fields, in order, as 21800 has them and as the SAT of any version writes
// them:
//   p pointer, i integer, d real, P position, V vector (three reals each), S string (from 7.0
//     "@n text", before it "n text"),
//   I an interval's end ("I": infinite → false; "F" and a real: finite → true and the real),
//   L/<false>/<true> a logical written as one of two words (before 7.0 also as 0 or 1),
//   L?/… one that may be missing, E/<table> an enum written as a word of the table,
//   q a 64-bit integer, w an identifier,
//   t the entity's tag and n its pattern pointer — the ENTITY data after the attribute pointer
//     from 7.0 (ENTITY_TAGS_VERSION, PATTERN_VERSION; the pattern for topology and geometry);
//     before 7.0 the SAB has -1 and $-1 there (AutoCAD's own SAB of ACadSharp's SAT 400 bodies,
//     sample_AC1027, and AutoCAD's chain for SAT 106 and 600),
//   c the coedge's integer — 21800 has it after the loop (or wire) pointer, the SAT before 21800
//     not: AutoCAD's SAB of their bodies has 0 there (ACadSharp's D65, D69, D6A; LibreDWG's 2E1),
//   a / b the edge's start / end parameter — when the SAT does not save them (1.1 … 4.0,
//     PARAM_VERSION; ACadSharp's SAT 400) the parameters of its vertices on its curve, along the
//     edge (negated when it is reversed; an ellipse's end after its start), as AutoCAD's SAB of
//     those bodies has them,
//   k the cone's u scale — when the SAT does not save it, the length of its major axis (the
//     chain's SAB of SAT 600 and 20800 cones),
//   G a tolerant coedge's 3D curve — 21800 has it after the curve pointer, the SAT not (SAT 600:
//     AutoCAD's chain computes it, a par_int_cur of the coedge's pcurve on its surface),
//   *p / *i the rest pointers / integers, R eye_refinement's names and values to "end_fields",
//   T/<kind> a subtype ("{ ref n }", or "{ name … }" by the name's list of that kind),
//   C / U a curve / a surface written inside a subtype: its identifier ("intcurve", "plane",
//     "null_surface" …) and the fields of its record ("intcurve-curve", "plane-surface") after
//     the ENTITY data "p t n" (AutoCAD's chain: straight, ellipse, intcurve; plane, cone, spline),
//   B / B2 a bs3 curve (3D) / a bs2 curve (2D), N a bs3 surface, D the discontinuities,
//   Q the int_cur data, Y the spl_sur data, X data AutoCAD's kernel does not know (strings typed,
//     the text between them as text, as the chain keeps it), A ATTRIB_CUSTOM's data by its
//     definition,
//   ~<version>:<code>:<default> a field the SAT has from that version on: before it, the SAB has
//     the default (the chain's SAB of ODA's SAT 21200 of the same bodies, of AutoCAD's SAT 106 …
//     20800),
//   ?<code>:<default> a field the SAT of some versions does not have, known by what stands there:
//     the field when the next value is of its kind, else the default (the chain's SAB).
// A default is an integer, a real, a pointer, a word, "I" or "F<real>" (an interval's end), or
// "x,y,z". Every record starts with its attribute pointer.
struct AcisFieldList {
    const char* type;
    const char* fields;
};

const AcisFieldList field_lists[] = {
    {"asmheader", "p t S"},
    {"body", "p t n p p p"},
    {"lump", "p t n p p p"},
    // Next, subshell, face, wire (from 1.7, WIREBOOL_VERSION: SAT 106's shells have none), lump
    {"shell", "p t n p p p ~107:p:-1 p"},
    // No oracle has a subshell (AutoCAD's chain refuses one, error 65000; no file has one): its
    // fields are pointers
    {"subshell", "p t n *p"},
    {"face", "p t n p p p p p L/forward/reversed L/single/double L?/out/in"},
    {"loop", "p t n p p p"},
    {"coedge", "p t n p p p p L/forward/reversed p c p"},
    // The convexity from 6.0 (SAT 600; 400 and 106 have none: "unknown", AutoCAD's SAB)
    {"edge", "p t n p a p b p p L/forward/reversed ?S:unknown"},
    // The vertex's integer from 21200 (the chain: 2 for the SAT before it, where AutoCAD's own SAB
    // of its own bodies has 0 or 1)
    {"vertex", "p t n p ?i:2 p"},
    // AutoCAD's chain: a body's wire "$-1 -1 $-1 $-1 $3 $1 $-1 out" becomes the shell's wire
    // "$ i $ $ $ $ $ F" (without "out", or without the last pointer as well: errors 75004, 75005)
    {"wire", "p t n p p p p L/out/in"},
    {"point", "p t n P"},
    {"transform", "p t V V V V d L/no_rotate/rotate L/no_reflect/reflect L/no_shear/shear"},
    {"plane-surface", "p t n P V V L/forward_v/reverse_v I I I I"},
    {"cone-surface", "p t n P V V d I I d d k L/forward/reversed I I I I"},
    // The u direction and the pole (missing in early ACIS: the x and z axes)
    {"sphere-surface", "p t n P d ?V:1,0,0 ?V:0,0,1 L/forward_v/reverse_v I I I I"},
    {"torus-surface", "p t n P V d d ?V:1,0,0 L/forward_v/reverse_v I I I I"},
    {"spline-surface", "p t n L/forward/reversed T/spl_sur I I I I"},
    {"straight-curve", "p t n P V I I"},
    {"ellipse-curve", "p t n P V V d I I"},
    {"intcurve-curve", "p t n L/forward/reversed T/int_cur I I"},
    // The curve in a surface's parameters: its type (0: its own subtype; the only one AutoCAD's
    // bodies have), its sense, the subtype, its offset
    {"pcurve", "p t n i L/forward/reversed T/par_cur d d"},
    // The tolerant topology (AutoCAD's chain of SAT 600, speaker.dxf; ODA's SAT 21500 of the
    // chain's DWG): a coedge with its parameter range, a curve pointer (21500) and its 3D curve
    // (G); an edge with its tolerance and an integer (the chain: 21301); a vertex with its
    // tolerance and two reals the kernel works out from the gaps to the vertex's edges — the SAT
    // 600 has neither: -1, which the kernel works them out for (the speaker's mass properties as
    // the chain's to the last digit; the tolerance there changes the products of inertia, 0 the
    // volume)
    {"tcoedge-coedge", "p t n p p p p L/forward/reversed p c p d d ?p:-1 G"},
    {"tedge-edge", "p t n p a p b p p L/forward/reversed ?S:unknown d ?i:21301"},
    {"tvertex-vertex", "p t n p ?i:2 p d ?d:-1 ?d:-1"},
    // Attributes: owner's attribute chain (next, previous), the owner, then their data
    {"persubent-acadSolidHistory-attrib", "p t p p p *i"},
    {"eye_refinement", "p t R"},
    {"vertex_template", "p t *i"},
    {"fmesh-eye-attrib", "p t p p p"},
    {"ptlist-eye-attrib", "p t p p p"},
    {"ref_vt-eye-attrib", "p t p p p p p"},
    {"color-adesk-attrib", "p t p p p i"},
    {"material-adesk-attrib", "p t p p p i i"},
    {"DXID-attrib", "p t p p p S S"},
    {"name_attrib-gen-attrib", "p t p p p E/split E/merge E/trans E/copy S"},
    {"string_attrib-name_attrib-gen-attrib", "p t p p p E/split E/merge E/trans E/copy S S"},
    {"integer_attrib-name_attrib-gen-attrib", "p t p p p E/split E/merge E/trans E/copy S i"},
    {"entity_attrib-name_attrib-gen-attrib", "p t p p p E/split E/merge E/trans E/copy S p"},
    {"int64_attrib-name_attrib-gen-attrib", "p t p p p E/split E/merge E/trans E/copy S q"},
    // AutoCAD's own SAB (apollo_3d, the f360 files): the definition's name and its data
    {"ATTRIB_CUSTOM-attrib", "p t p p p S A"},
    // What AutoCAD's kernel does not know: the chain keeps the data as text (ODA's SAT spells
    // ATTRIB_CUSTOM in lower case; Spatial's rgb_color, ODA's id-tsl)
    {"attrib_custom-attrib", "p t p p p X"},
    {"rgb_color-st-attrib", "p t p p p X"},
    {"id-tsl-attrib", "p t p p p X"},
};

// A subtype's fields by its kind and name. int_cur's data (Q) and spl_sur's (Y) as the chain
// has them: the version (21200: none, the SAB 20900), the approximation, the bs3 data, …
struct AcisSubtypeList {
    const char* kind;
    const char* name;
    const char* fields;
};

// int_cur: the approximation (from 5.0, APPROX_SUMMARY_VERSION; SAT 106 has none: full), the bs3
// curve and its fit tolerance, the two surfaces, the two pcurves, the range (SAT 106: none,
// infinite), the discontinuities (from 3.0, DISCONTINUITY_VERSION; SAT 106: those of the bs3
// curve, as the chain works them out), an integer (from 21200; the chain: 0)
const char* const int_cur_data = "K B d U U B2 B2 ?I:I ?I:I ?D:curve ?i:0";

const AcisSubtypeList subtype_lists[] = {
    // The version (from 21500; before it the SAB has 20900), the data, the safe range (SAT 106:
    // none, the chain [1, 0]), the extensions (from 21500; the SAB 0)
    {"int_cur", "exact_int_cur", "?i:20900 Q ?I:F1 ?I:F0 ?E/int_ext:0 ?E/int_ext:0"},
    {"int_cur", "int_int_cur", "?i:20900 Q"},
    // The 3D curve of a pcurve on its surface (the chain's, of a tolerant coedge): the data and
    // two logicals (the chain: T F)
    {"int_cur", "par_int_cur", "?i:23100 Q ?L/F/T:T ?L/F/T:F"},
    // The version (from 21200; the SAB 20900), the profile (and the second curve), the
    // direction or the base point, the spl_sur data, (exact_spl_sur) the ranges and the extension
    // (from 21200; the SAB 0)
    {"spl_sur", "exact_spl_sur", "?i:20900 Y I I I I ?E/spl_ext:0"},
    {"spl_sur", "cyl_spl_sur", "i C V P Y"},
    // ODA's SAB (klonk_base); AutoCAD's chain makes it a cyl_spl_sur
    {"spl_sur", "sum_spl_sur", "?i:20900 C C P Y"},
    // The profile, the axis' root and direction (the chain's SAB of SAT 600 and 700 rotsur)
    {"spl_sur", "rot_spl_sur", "?i:20900 C P V Y"},
    // The bs2 curve, its fit tolerance, the surface
    {"par_cur", "exp_par_cur", "B2 d U"},
};

// The save identifiers of the subtypes before ASM (SAT 106 … 20800, ACIS 7.0's "SAT Save File
// Format": "exactsur … uses data element spl_sur") and the names 21800 has for them — AutoCAD's
// chain writes the SAT 600/700 exactcur as exact_int_cur, surfintcur as int_int_cur, exactsur as
// exact_spl_sur, rotsur as rot_spl_sur, exppc as exp_par_cur
struct AcisSubtypeName {
    const char* old_name;
    const char* name;
};

const AcisSubtypeName old_subtype_names[] = {
    {"exactcur", "exact_int_cur"},
    {"surfintcur", "int_int_cur"},
    {"parcur", "par_int_cur"},
    {"exactsur", "exact_spl_sur"},
    {"rotsur", "rot_spl_sur"},
    {"sumsur", "sum_spl_sur"},
    {"exppc", "exp_par_cur"},
};

// The asmheader the SAT before 20800 does not have (B63): ODA's and ezdxf's ASM version
const char* const default_asm_version = "208.0.4.7009";

// Words written as enums: a name attribute's actions (split, merge, transform, copy) as
// AutoCAD's SAB has them (int64_attrib "copy keep_all ignore copy" → 2, 4, 1, 2; the name,
// integer, string and entity attributes of jerwil, kolja, safecast, the 3dsolids and klonk), the
// approximation level, a bs3's closure and singularity (in capitals in ODA's sum_spl_sur and in
// SAT 600's rotsur) and the extensions of an int_cur and an spl_sur. The values no body has are
// ODA's words for them: a SAB with the value made SAT 21500 by ODA (closed, low, high, the
// capitals' SINGULAR_LOW …, EXTEND_213_G2, EXTEND_G1, EXTENDED_G1, VERSION_212_G2; ODA drops a body
// with an int_cur extension above 4 and fails on an spl_sur one above 2)
struct AcisEnumWord {
    const char* table;
    const char* word;
    int value;
};

const AcisEnumWord enum_words[] = {
    {"split", "lose", 0},
    {"split", "keep", 1},
    {"split", "copy", 2},
    {"merge", "lose", 0},
    {"merge", "keep_kept", 1},
    {"merge", "keep_all", 4},
    {"merge", "custom", 5},
    {"trans", "ignore", 1},
    {"trans", "apply", 2},
    {"copy", "copy", 2},
    {"approx", "full", 0},
    {"approx", "none", 2},
    {"closure", "open", 0},
    {"closure", "closed", 1},
    {"closure", "periodic", 2},
    {"closure", "OPEN", 0},
    {"closure", "CLOSED", 1},
    {"closure", "PERIODIC", 2},
    {"singularity", "none", 0},
    {"singularity", "low", 1},
    {"singularity", "high", 2},
    {"singularity", "both", 3},
    {"singularity", "NON_SINGULAR", 0},
    {"singularity", "SINGULAR_LOW", 1},
    {"singularity", "SINGULAR_HIGH", 2},
    {"singularity", "SINGULAR_BOTH", 3},
    {"int_ext", "EXTEND_213", 0},
    {"int_ext", "EXTEND_213_G2", 1},
    {"int_ext", "EXTEND_214_G2", 2},
    {"int_ext", "EXTEND_G1", 3},
    {"int_ext", "UNEXTENDED", 4},
    {"spl_ext", "UNEXTENDED", 0},
    {"spl_ext", "EXTENDED_G1", 1},
    {"spl_ext", "VERSION_212_G2", 2},
};

// eye_refinement's fields: a name and its value, an integer or a real by the name (AutoCAD's
// SAB: SAT 21500's "@4 grid  1 … @4 stol 9.62…" → "grid " 4 1 … "stol" 6 9.62…)
const char* const eye_integer_fields[] = {"grid", "tri", "surf", "adj", "grad", "postcheck",
                                          "calc", "conv", "mgrid", "ugrid", "vgrid"};
const char* const eye_real_fields[] = {"stol", "ntol", "dsil", "flatness", "pixarea", "hmax", "gridar"};

// The SAB being written: tagged values, little-endian
class SabWriter {
public:
    explicit SabWriter(std::vector<uint8_t>& out) : out_(out) {}

    void raw_int32(int32_t v) {
        uint32_t u = static_cast<uint32_t>(v);
        for (int i = 0; i < 4; ++i) out_.push_back(static_cast<uint8_t>(u >> (8 * i)));
    }
    void raw_bytes(const char* s, size_t n) { out_.insert(out_.end(), s, s + n); }
    void integer(int32_t v) { tag(4); raw_int32(v); }
    void integer64(int64_t v) {
        tag(23);
        uint64_t u = static_cast<uint64_t>(v);
        for (int i = 0; i < 8; ++i) out_.push_back(static_cast<uint8_t>(u >> (8 * i)));
    }
    void real(double v) { tag(6); raw_double(v); }
    void pointer(int32_t v) { tag(12); raw_int32(v); }
    void enumeration(int32_t v) { tag(21); raw_int32(v); }
    void logical(bool v) { tag(v ? 10 : 11); }
    void triple(bool position, const double v[3]) {
        tag(position ? 19 : 20);
        for (int i = 0; i < 3; ++i) raw_double(v[i]);
    }
    // A string: its length in 1, 2 or 4 bytes (AutoCAD's are all shorter than 256: 7)
    void string(const std::string& s) {
        size_t n = s.size();
        size_t width = n < 0x100 ? 1 : (n < 0x10000 ? 2 : 4);
        tag(width == 1 ? 7 : (width == 2 ? 8 : 9));
        for (size_t i = 0; i < width; ++i) out_.push_back(static_cast<uint8_t>(n >> (8 * i)));
        raw_bytes(s.data(), n);
    }
    // Text kept as it was (tag 18, a 4-byte length)
    void text(const std::string& s) {
        tag(18);
        raw_int32(static_cast<int32_t>(s.size()));
        raw_bytes(s.data(), s.size());
    }
    // A word of the data: "nubs", "null_surface", a subtype's name
    void identifier(const std::string& w) {
        tag(13);
        out_.push_back(static_cast<uint8_t>(w.size()));
        raw_bytes(w.data(), w.size());
    }
    void subtype_start() { tag(15); }
    void subtype_end() { tag(16); }
    // A record's name: "plane-surface" is 14 "plane", 13 "surface"
    void name(const std::string& type) {
        size_t a = 0;
        while (true) {
            size_t dash = type.find('-', a);
            bool last = dash == std::string::npos;
            std::string part = type.substr(a, last ? std::string::npos : dash - a);
            tag(last ? 13 : 14);
            out_.push_back(static_cast<uint8_t>(part.size()));
            raw_bytes(part.data(), part.size());
            if (last) break;
            a = dash + 1;
        }
    }
    void end_record() { tag(17); }

private:
    void tag(uint8_t t) { out_.push_back(t); }
    void raw_double(double v) {
        uint64_t u;
        std::memcpy(&u, &v, sizeof u);
        for (int i = 0; i < 8; ++i) out_.push_back(static_cast<uint8_t>(u >> (8 * i)));
    }
    std::vector<uint8_t>& out_;
};

// The SAT header: "version records entities flags", the product, the ACIS version and the date
// as counted strings ("16 Autodesk AutoCAD 20 ASM 230.7.0.65535 NT 0 "), and the unit (mm per
// unit), resabs and resnor
struct SatHeader {
    int version = 0;
    std::string product;
    std::string acis_version;
    std::string date;
    double units = 1.0;
    double resabs = 1e-06;
    double resnor = 1e-10;
};

bool parse_long(const std::string& s, long long& v) {
    if (s.empty()) return false;
    const char* first = s.data();
    if (*first == '+') ++first;
    auto r = std::from_chars(first, s.data() + s.size(), v);
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}

bool parse_real(const std::string& s, double& v) {
    if (s.empty()) return false;
    const char* first = s.data();
    if (*first == '+') ++first;
    auto r = std::from_chars(first, s.data() + s.size(), v);
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}

bool is_blank(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

bool read_header(const std::string& sat, SatHeader& h) {
    std::vector<std::string> lines;
    size_t pos = 0;
    for (int i = 0; i < 3 && pos <= sat.size(); ++i) {
        size_t e = sat.find('\n', pos);
        if (e == std::string::npos) e = sat.size();
        lines.push_back(sat.substr(pos, e - pos));
        pos = e + 1;
    }
    if (lines.empty()) return false;
    auto words = [](const std::string& line) {
        std::vector<std::string> out;
        size_t a = 0;
        while (true) {
            a = line.find_first_not_of(" \t\r", a);
            if (a == std::string::npos) break;
            size_t b = line.find_first_of(" \t\r", a);
            out.push_back(line.substr(a, b == std::string::npos ? std::string::npos : b - a));
            if (b == std::string::npos) break;
            a = b;
        }
        return out;
    };
    std::vector<std::string> first = words(lines[0]);
    long long version = 0;
    if (first.empty() || !parse_long(first[0], version)) return false;
    h.version = static_cast<int>(version);
    // Before 2.0 (FILEINFO_VERSION) the header is the first line: no product, no ACIS version,
    // no date, the unit 1 and the default resabs and resnor (ODA's SAB of SAT 106)
    if (h.version < 200) return true;
    if (lines.size() < 3) return false;
    // Three counted strings: the count, a space, the characters (ODA writes the count as a
    // string's, "@33 Open Design Alliance ACIS Builder")
    const std::string& info = lines[1];
    size_t at = 0;
    std::string* fields[] = {&h.product, &h.acis_version, &h.date};
    for (std::string* f : fields) {
        while (at < info.size() && info[at] == ' ') ++at;
        if (at < info.size() && info[at] == '@') ++at;
        size_t b = at;
        while (b < info.size() && info[b] >= '0' && info[b] <= '9') ++b;
        long long n = 0;
        if (b == at || !parse_long(info.substr(at, b - at), n) || n < 0) return false;
        at = b < info.size() ? b + 1 : b;
        if (at + static_cast<size_t>(n) > info.size()) return false;
        *f = info.substr(at, static_cast<size_t>(n));
        at += static_cast<size_t>(n);
    }
    std::vector<std::string> units = words(lines[2]);
    if (units.size() < 3 || !parse_real(units[0], h.units) || !parse_real(units[1], h.resabs) ||
        !parse_real(units[2], h.resnor)) {
        return false;
    }
    return true;
}

// What the records being written share: the SAT, its version, whether the asmheader is put in
// front of the records (every pointer + 1), and the subtypes' numbers in the SAB — the SAT's
// definitions in order, and those the encoder writes besides them (a tolerant coedge's curve)
// The DXF version a SAB format is written for, for the errors
const char* target_name(int format) {
    return format == sab_format_asm ? "DXF 2018" : "DXF 2013";
}

struct SabContext {
    const AcisFile* file = nullptr;
    const std::string* sat = nullptr;
    int version = 0;
    int format = sab_format_acis;      // the SAB's format version: 21800 or 22300
    int shift = 0;
    std::map<std::pair<size_t, size_t>, size_t> definitions;   // (record, token of "{") → k
    std::vector<int> subtype_number;    // the SAT's k-th definition → its number in the SAB
    int subtypes = 0;                   // definitions written
};

// A bs3 or bs2 curve as it was written: the degree, the knots (ACIS's: without the first and
// the last) with their multiplicities, the control points (and weights)
struct SabSplineData {
    int degree = 0;
    int dimension = 3;
    bool rational = false;
    std::vector<double> knots;
    std::vector<int> multiplicities;
    std::vector<double> coordinates;    // dimension (+ 1 rational) per point
};

// The discontinuities of a bs3 curve the SAT does not save (before 3.0, DISCONTINUITY_VERSION):
// at a knot of multiplicity m inside the curve the derivative of order degree - m + 1 may jump,
// so the knot is in the list of that order (1, 2 or 3; a knot of the degree's multiplicity or
// more in the first). AutoCAD's chain lists the same knots of orders 1 and 2 for SAT 106's
// exactcur, and of order 3 those where its own evaluation finds a jump — a part of these
// (curves made by inserting knots, whose third derivative barely jumps); the list here says
// where the curve may not be smooth, all of them.
void spline_discontinuities(const SabSplineData& s, std::vector<double> lists[3]) {
    for (size_t i = 1; i + 1 < s.knots.size(); ++i) {
        int order = std::max(1, s.degree - s.multiplicities[i] + 1);
        if (order <= 3) lists[order - 1].push_back(s.knots[i]);
    }
}

// A knot line of a surface a pcurve crosses: the parameter (0 u, 1 v), its value, the order of
// the discontinuity there
struct SabKnotLine {
    int coordinate = 0;
    double value = 0.0;
    int order = 0;
};

// One record written by its field list; false with the error
class RecordEncoder {
public:
    RecordEncoder(SabContext& ctx, size_t index, SabWriter& out, size_t pos = 0, bool copy = false)
        : ctx_(ctx), record_(ctx.file->records[index]), index_(index), version_(ctx.version), sat_(*ctx.sat),
          out_(out), pos_(pos), copy_(copy) {}

    bool encode(const char* fields, std::string& error) {
        if (!run(fields) || pos_ != record_.tokens.size()) {
            error = special_error_.empty() ? fields_error() : special_error_;
            return false;
        }
        out_.end_record();
        return true;
    }

    // A subtype or a surface written somewhere else (a tolerant coedge's curve): from pos
    bool subtype_here(const std::string& kind, std::string& error) {
        if (subtype(kind)) return true;
        error = special_error_.empty() ? fields_error() : special_error_;
        return false;
    }
    bool surface_here(std::string& error) {
        if (inline_geometry("surface")) return true;
        error = special_error_.empty() ? fields_error() : special_error_;
        return false;
    }

private:
    std::string fields_error() const {
        return "the ACIS record " + std::to_string(index_) + " (\"" + record_.type +
               "\") does not have the fields of its type in version " + std::to_string(version_);
    }
    // A word the oracles' bodies do not have: the error names it
    bool unknown(const std::string& what, const std::string& word) {
        special_error_ = "the ACIS " + what + " \"" + word + "\" (record " + std::to_string(index_) +
                         ", \"" + record_.type + "\") cannot be written as " + target_name(ctx_.format);
        return false;
    }

    // A field list: its codes separated by spaces
    bool run(const std::string& list) {
        size_t a = 0;
        while (a < list.size()) {
            size_t b = list.find(' ', a);
            std::string field = list.substr(a, b == std::string::npos ? std::string::npos : b - a);
            a = b == std::string::npos ? list.size() : b + 1;
            if (!field_value(field)) return false;
        }
        return true;
    }

    const AcisToken* peek(size_t ahead = 0) const {
        return pos_ + ahead < record_.tokens.size() ? &record_.tokens[pos_ + ahead] : nullptr;
    }
    bool at_type(AcisTokenType type) const {
        const AcisToken* t = peek();
        return t && t->type == type;
    }
    bool at_word(const std::string& w) const {
        const AcisToken* t = peek();
        return t && t->type == AcisTokenType::Word && t->text == w;
    }
    bool integer_value(long long& v) {
        const AcisToken* t = peek();
        if (!t || t->type != AcisTokenType::Number) return false;
        if (!t->text.empty()) {
            if (!parse_long(t->text, v)) return false;
        } else {
            if (t->number != std::floor(t->number)) return false;
            v = static_cast<long long>(t->number);
        }
        ++pos_;
        return true;
    }
    bool int32_value(int32_t& v) {
        long long n = 0;
        size_t before = pos_;
        if (!integer_value(n)) return false;
        if (n < std::numeric_limits<int32_t>::min() || n > std::numeric_limits<int32_t>::max()) {
            pos_ = before;
            return false;
        }
        v = static_cast<int32_t>(n);
        return true;
    }
    bool real_value(double& v) {
        const AcisToken* t = peek();
        if (!t || t->type != AcisTokenType::Number) return false;
        v = t->number;
        ++pos_;
        return true;
    }
    bool word_value(std::string& w) {
        const AcisToken* t = peek();
        if (!t || t->type != AcisTokenType::Word) return false;
        w = t->text;
        ++pos_;
        return true;
    }
    // A string: "@n text" (a String token), or before 7.0 (AT_VERSION) "n text" — the count,
    // a space and n characters, which the reader took as a number and words
    bool string_value(std::string& s) {
        const AcisToken* t = peek();
        if (!t) return false;
        if (t->type == AcisTokenType::String) {
            s = t->text;
            ++pos_;
            return true;
        }
        long long n = 0;
        if (version_ >= 700 || t->type != AcisTokenType::Number || !parse_long(t->text, n) || n < 0) return false;
        size_t from = t->end + 1;
        if (from + static_cast<size_t>(n) > record_.end) return false;
        s = sat_.substr(from, static_cast<size_t>(n));
        ++pos_;
        while (const AcisToken* x = peek()) {
            if (x->begin >= from + static_cast<size_t>(n)) break;
            ++pos_;
        }
        return true;
    }
    bool integer_field() {
        int32_t v = 0;
        if (!int32_value(v)) return false;
        out_.integer(v);
        return true;
    }
    bool real_field() {
        double v = 0.0;
        if (!real_value(v)) return false;
        out_.real(v);
        return true;
    }
    void pointer(int32_t v) { out_.pointer(v >= 0 ? v + ctx_.shift : v); }

    // "L/false/true" (or "L?/…"): the word and the two words it may be; before 7.0 a logical may
    // be 0 or 1 (SAT 106's coedges and plane senses)
    bool logical(const std::string& field, bool optional) {
        size_t s1 = field.find('/');
        size_t s2 = field.find('/', s1 + 1);
        std::string false_word = field.substr(s1 + 1, s2 - s1 - 1);
        std::string true_word = field.substr(s2 + 1);
        if (optional && !peek()) return true;
        const AcisToken* t = peek();
        if (t && t->type == AcisTokenType::Number && version_ < 700 && (t->number == 0.0 || t->number == 1.0)) {
            out_.logical(t->number != 0.0);
            ++pos_;
            return true;
        }
        std::string w;
        if (!word_value(w)) return false;
        if (w == false_word) out_.logical(false);
        else if (w == true_word) out_.logical(true);
        else return false;
        return true;
    }

    bool interval_end() {
        std::string w;
        if (!word_value(w)) return false;
        if (w == "I") {
            out_.logical(false);
            return true;
        }
        double v = 0.0;
        if (w != "F" || !real_value(v)) return false;
        out_.logical(true);
        out_.real(v);
        return true;
    }

    static bool in_table(const std::string& table, const std::string& w) {
        for (const auto& e : enum_words) {
            if (table == e.table && w == e.word) return true;
        }
        return false;
    }

    bool enumeration(const std::string& table) {
        std::string w;
        if (!word_value(w)) return false;
        for (const auto& e : enum_words) {
            if (table == e.table && w == e.word) {
                out_.enumeration(e.value);
                return true;
            }
        }
        return unknown("word", w);
    }

    bool identifier() {
        std::string w;
        if (!word_value(w)) return false;
        out_.identifier(w);
        return true;
    }

    // eye_refinement: name, value … "end_fields"
    bool eye_fields() {
        while (true) {
            std::string name;
            if (!string_value(name)) return false;
            out_.string(name);
            size_t e = name.find_last_not_of(' ');
            std::string key = e == std::string::npos ? std::string() : name.substr(0, e + 1);
            if (key == "end_fields") return true;
            bool known = false;
            for (const char* f : eye_integer_fields) {
                if (key == f) {
                    if (!integer_field()) return false;
                    known = true;
                }
            }
            for (const char* f : eye_real_fields) {
                if (key == f) {
                    if (!real_field()) return false;
                    known = true;
                }
            }
            if (!known) return false;
        }
    }

    // A bs3 curve's or bs2 curve's knots: (value, multiplicity) pairs; their multiplicities' sum
    bool knots(int32_t count, long long& sum, SabSplineData* data) {
        sum = 0;
        for (int32_t k = 0; k < count; ++k) {
            double v = 0.0;
            int32_t mult = 0;
            if (!real_value(v) || !int32_value(mult) || mult < 1) return false;
            out_.real(v);
            out_.integer(mult);
            sum += mult;
            if (data) {
                data->knots.push_back(v);
                data->multiplicities.push_back(mult);
            }
        }
        return true;
    }

    // The number of control points: ACIS leaves out the first and the last knot (the whole knot
    // vector has the sum + 2), so sum + 2 - degree - 1
    static long long control_points(long long sum, int32_t degree) { return sum + 1 - degree; }

    // bs3 curve (dimension 3) or bs2 curve (2): nullbs, or nubs | nurbs, the degree, the closure,
    // the number of knots, the knots, the control points (nurbs: each with its weight)
    bool bs_curve(int dimension) {
        std::string kind;
        if (!word_value(kind)) return false;
        out_.identifier(kind);
        if (dimension == 3) last_spline_ = SabSplineData{};
        if (kind == "nullbs") return true;
        if (kind != "nubs" && kind != "nurbs") return false;
        int32_t degree = 0;
        int32_t count = 0;
        long long sum = 0;
        if (!int32_value(degree) || degree < 1) return false;
        out_.integer(degree);
        if (!enumeration("closure")) return false;
        if (!int32_value(count) || count < 2) return false;
        out_.integer(count);
        SabSplineData data;
        data.degree = degree;
        data.dimension = dimension;
        data.rational = kind == "nurbs";
        if (!knots(count, sum, &data)) return false;
        long long points = control_points(sum, degree);
        if (points < 1) return false;
        int per_point = dimension + (kind == "nurbs" ? 1 : 0);
        for (long long n = 0; n < points * per_point; ++n) {
            double v = 0.0;
            if (!real_value(v)) return false;
            out_.real(v);
            data.coordinates.push_back(v);
        }
        if (dimension == 3) last_spline_ = data;
        return true;
    }

    // bs3 surface: nullbs, or nubs | nurbs, the two degrees, (nurbs: which directions are
    // rational, an identifier — "both"), the closures, the singularities, the numbers of knots,
    // the u knots, the v knots, the control points (nurbs: each with its weight)
    bool bs_surface() {
        std::string kind;
        if (!word_value(kind)) return false;
        out_.identifier(kind);
        if (kind == "nullbs") return true;
        if (kind != "nubs" && kind != "nurbs") return false;
        int32_t degree_u = 0, degree_v = 0, count_u = 0, count_v = 0;
        long long sum_u = 0, sum_v = 0;
        if (!int32_value(degree_u) || degree_u < 1) return false;
        out_.integer(degree_u);
        if (!int32_value(degree_v) || degree_v < 1) return false;
        out_.integer(degree_v);
        if (kind == "nurbs" && !identifier()) return false;
        if (!enumeration("closure") || !enumeration("closure")) return false;
        if (!enumeration("singularity") || !enumeration("singularity")) return false;
        if (!int32_value(count_u) || count_u < 2) return false;
        out_.integer(count_u);
        if (!int32_value(count_v) || count_v < 2) return false;
        out_.integer(count_v);
        if (!knots(count_u, sum_u, nullptr) || !knots(count_v, sum_v, nullptr)) return false;
        long long points = control_points(sum_u, degree_u) * control_points(sum_v, degree_v);
        if (points < 1) return false;
        int per_point = 3 + (kind == "nurbs" ? 1 : 0);
        for (long long n = 0; n < points * per_point; ++n) {
            if (!real_field()) return false;
        }
        return true;
    }

    // The discontinuities of the three orders: each a count and the parameters
    bool discontinuities() {
        for (int order = 0; order < 3; ++order) {
            int32_t count = 0;
            if (!int32_value(count) || count < 0) return false;
            out_.integer(count);
            for (int32_t k = 0; k < count; ++k) {
                if (!real_field()) return false;
            }
        }
        return true;
    }

    // Those the SAT does not save: the bs3 curve's ("curve"), or none
    void computed_discontinuities(const std::string& which) {
        std::vector<double> lists[3];
        if (which == "curve" && last_spline_.degree > 0) spline_discontinuities(last_spline_, lists);
        for (const auto& l : lists) {
            out_.integer(static_cast<int32_t>(l.size()));
            for (double u : l) out_.real(u);
        }
    }

    // int_cur's approximation level: "full" (SAB 0; missing before 5.0: full). "summary" and
    // "none" no oracle has — no file has them, ODA drops a body with them, and AutoCAD's core
    // reads no int_cur with them (errors 75004, 75005, 75007, 75013 for the forms tried): an error
    // that names the word
    bool curve_approximation() {
        const AcisToken* t = peek();
        if (t && t->type == AcisTokenType::Word && (t->text == "summary" || t->text == "none")) {
            return unknown("word", t->text);
        }
        return field_value("?E/approx:0");
    }

    // spl_sur's data: the approximation (from 5.0) — "full": the bs3 surface and its fit
    // tolerance; "none": the u and v ranges, the closures and the singularities (ODA's
    // sum_spl_sur, the rotsur of SAT 600 and 700) — the discontinuities in u and in v (from 3.0),
    // and (from 21500) a logical "F"
    bool spl_data() {
        const AcisToken* t = peek();
        if (!t || t->type != AcisTokenType::Word) return false;
        if (t->text == "summary") return unknown("word", t->text);
        std::string level = in_table("approx", t->text) ? t->text : "full";
        if (!field_value("?E/approx:0")) return false;
        if (level == "full") {
            if (!bs_surface() || !real_field()) return false;
        } else {
            for (int k = 0; k < 4; ++k) {
                if (!interval_end()) return false;
            }
            if (!enumeration("closure") || !enumeration("closure")) return false;
            if (!enumeration("singularity") || !enumeration("singularity")) return false;
        }
        return field_value("?D:none") && field_value("?D:none") && field_value("~21500:L/F/T:F");
    }

    // A subtype of a kind: "{ ref n }" or "{ name fields }" — an old name written as 21800's.
    // A reference is to the definition's number in the SAB; in a copy (a tolerant coedge's
    // curve) the definition is written again in its place.
    bool subtype(const std::string& kind) {
        if (!at_type(AcisTokenType::SubtypeStart)) return false;
        size_t start = pos_;
        if (at_word_after("ref")) {
            pos_ += 2;
            int32_t n = 0;
            if (!int32_value(n) || n < 0 || static_cast<size_t>(n) >= ctx_.subtype_number.size()) return false;
            if (!at_type(AcisTokenType::SubtypeEnd)) return false;
            ++pos_;
            if (copy_) {
                const AcisSubtype& d = ctx_.file->subtypes[static_cast<size_t>(n)];
                if (depth_ > 32 || d.record >= ctx_.file->records.size()) return false;
                RecordEncoder inner(ctx_, d.record, out_, d.token, true);
                inner.depth_ = depth_ + 1;
                if (!inner.subtype(kind)) {
                    special_error_ = inner.special_error_;
                    return false;
                }
                return true;
            }
            int number = ctx_.subtype_number[static_cast<size_t>(n)];
            if (number < 0) return false;
            out_.subtype_start();
            out_.identifier("ref");
            out_.integer(number);
            out_.subtype_end();
            return true;
        }
        ++pos_;
        if (!copy_) {
            auto it = ctx_.definitions.find({index_, start});
            if (it != ctx_.definitions.end()) ctx_.subtype_number[it->second] = ctx_.subtypes;
        }
        ++ctx_.subtypes;
        out_.subtype_start();
        std::string name;
        if (!word_value(name)) return false;
        for (const auto& o : old_subtype_names) {
            if (name == o.old_name) name = o.name;
        }
        out_.identifier(name);
        const char* fields = nullptr;
        for (const auto& s : subtype_lists) {
            if (kind == s.kind && name == s.name) fields = s.fields;
        }
        if (!fields) return unknown("subtype", name);
        if (!run(fields)) return false;
        if (!at_type(AcisTokenType::SubtypeEnd)) return false;
        ++pos_;
        out_.subtype_end();
        return true;
    }
    bool at_word_after(const char* w) const {
        const AcisToken* t = peek(1);
        return t && t->type == AcisTokenType::Word && t->text == w;
    }

    // A curve or a surface written inside a subtype: its identifier, then its record's fields
    // after the ENTITY data
    bool inline_geometry(const char* base) {
        std::string w;
        if (!word_value(w)) return false;
        out_.identifier(w);
        if (std::string(base) == "surface" && w == "null_surface") return true;
        std::string type = w + "-" + base;
        for (const auto& l : field_lists) {
            std::string fields = l.fields;
            if (type == l.type && fields.rfind("p t n ", 0) == 0) return run(fields.substr(6));
        }
        return unknown(base, w);
    }

    // Data AutoCAD's kernel does not know, as the chain keeps it: the strings typed, what stands
    // between them — from after the space that follows the field before, up to the next string or
    // the record's end — as text (rgb_color's "0 1 0 ", id-tsl's "400250  ")
    bool unknown_data() {
        if (pos_ == 0) return false;
        size_t start = record_.tokens[pos_ - 1].end;
        if (start < sat_.size() && is_blank(sat_[start])) ++start;
        while (const AcisToken* t = peek()) {
            if (t->type == AcisTokenType::Pointer) return false;
            if (t->type == AcisTokenType::String) {
                if (t->begin > start) out_.text(sat_.substr(start, t->begin - start));
                out_.string(t->text);
                start = t->end;
                if (start < sat_.size() && is_blank(sat_[start])) ++start;
            }
            ++pos_;
        }
        if (record_.end > start) out_.text(sat_.substr(start, record_.end - start));
        return true;
    }

    // ATTRIB_CUSTOM's data after its definition's name, as AutoCAD's SAB has it: the time stamp
    // an integer and a real, the tags and the sketches integers and strings
    bool custom_data() {
        if (last_string_ == "Timestamp_attrib_def") return run("i d");
        if (last_string_ != "generic_tag_attrib_def" && last_string_ != "sketch_attrib_def") {
            return unknown("attribute definition", last_string_);
        }
        while (const AcisToken* t = peek()) {
            if (t->type == AcisTokenType::String) {
                out_.string(t->text);
                ++pos_;
            } else if (!integer_field()) {
                return false;
            }
        }
        return true;
    }

    // The edge's start and end parameters when the SAT does not save them: its vertices'
    // parameters on its curve, along the edge — after the start vertex stand the end vertex,
    // the coedge, the curve and the sense
    bool edge_parameters() {
        const AcisToken* start = pos_ > 0 ? &record_.tokens[pos_ - 1] : nullptr;
        const AcisToken* end = peek();
        const AcisToken* curve = peek(2);
        const AcisToken* sense = peek(3);
        if (!start || !end || !curve || !sense || start->type != AcisTokenType::Pointer ||
            end->type != AcisTokenType::Pointer || curve->type != AcisTokenType::Pointer) return false;
        bool reversed = sense->type == AcisTokenType::Word ? sense->text == "reversed" : sense->number != 0.0;
        AcisCurve c;
        Point3 p0, p1;
        if (start->number < 0 || end->number < 0 || curve->number < 0) return false;
        if (!acis_record_curve(*ctx_.file, static_cast<size_t>(curve->number), c) ||
            !acis_vertex_point(*ctx_.file, static_cast<size_t>(start->number), p0) ||
            !acis_vertex_point(*ctx_.file, static_cast<size_t>(end->number), p1)) return false;
        double a = acis_curve_parameter(c, p0);
        double b = acis_curve_parameter(c, p1);
        if (reversed) {
            a = -a;
            b = -b;
        }
        const double two_pi = 6.283185307179586;
        if (c.type == AcisCurveType::Ellipse) {
            if (start->number == end->number) b = a + two_pi;
            while (b <= a) b += two_pi;
        }
        out_.real(a);
        edge_end_ = b;
        have_edge_end_ = true;
        return true;
    }

    // A tolerant coedge's 3D curve: when the SAT has it, its integer (1) and the curve inside
    // it; else the coedge's pcurve on its surface as a par_int_cur (computed_coedge_curve())
    bool coedge_curve() {
        if (at_type(AcisTokenType::Number)) return field_value("i") && inline_geometry("curve");
        return computed_coedge_curve();
    }
    bool computed_coedge_curve();

    // The default of a field the SAT does not have
    bool default_value(const std::string& code, const std::string& value) {
        if (code == "i" || code.rfind("E/", 0) == 0) {
            long long v = 0;
            if (!parse_long(value, v)) return false;
            if (code == "i") out_.integer(static_cast<int32_t>(v));
            else out_.enumeration(static_cast<int32_t>(v));
            return true;
        }
        if (code == "p") {
            long long v = 0;
            if (!parse_long(value, v)) return false;
            out_.pointer(static_cast<int32_t>(v));
            return true;
        }
        if (code == "d") {
            double v = 0.0;
            if (!parse_real(value, v)) return false;
            out_.real(v);
            return true;
        }
        if (code.rfind("L/", 0) == 0) {
            out_.logical(value == code.substr(code.rfind('/') + 1));
            return true;
        }
        if (code == "I") {
            if (value == "I") {
                out_.logical(false);
                return true;
            }
            double v = 0.0;
            if (value.empty() || value[0] != 'F' || !parse_real(value.substr(1), v)) return false;
            out_.logical(true);
            out_.real(v);
            return true;
        }
        if (code == "S") {
            out_.string(value);
            return true;
        }
        if (code == "V" || code == "P") {
            double v[3];
            size_t a = 0;
            for (int k = 0; k < 3; ++k) {
                size_t b = value.find(',', a);
                if (!parse_real(value.substr(a, b == std::string::npos ? std::string::npos : b - a), v[k])) return false;
                a = b == std::string::npos ? value.size() : b + 1;
            }
            out_.triple(code == "P", v);
            return true;
        }
        if (code == "D") {
            computed_discontinuities(value);
            return true;
        }
        return false;
    }

    // Whether the next value is of the field's kind (a field that may be missing)
    bool fits(const std::string& code) const {
        const AcisToken* t = peek();
        if (!t) return false;
        if (code == "i" || code == "d" || code == "D") return t->type == AcisTokenType::Number;
        if (code == "p") return t->type == AcisTokenType::Pointer;
        if (code == "V" || code == "P") {
            for (size_t k = 0; k < 3; ++k) {
                const AcisToken* x = peek(k);
                if (!x || x->type != AcisTokenType::Number) return false;
            }
            return true;
        }
        if (code == "I") return t->type == AcisTokenType::Word && (t->text == "I" || t->text == "F");
        if (code == "S") {
            if (t->type == AcisTokenType::String) return true;
            return version_ < 700 && t->type == AcisTokenType::Number && peek(1) != nullptr;
        }
        if (code.rfind("E/", 0) == 0) return t->type == AcisTokenType::Word && in_table(code.substr(2), t->text);
        if (code.rfind("L/", 0) == 0) {
            size_t s1 = code.find('/');
            size_t s2 = code.find('/', s1 + 1);
            return t->type == AcisTokenType::Word &&
                   (t->text == code.substr(s1 + 1, s2 - s1 - 1) || t->text == code.substr(s2 + 1));
        }
        return false;
    }

    // "~version:code:default": the field from that version on; before it the default.
    // "?code:default": the field when the next value is of its kind; else the default.
    bool later_field(const std::string& field) {
        size_t first = field.find(':');
        size_t last = field.rfind(':');
        long long from = 0;
        if (first == std::string::npos || last == first || !parse_long(field.substr(1, first - 1), from)) return false;
        std::string code = field.substr(first + 1, last - first - 1);
        if (version_ >= from) return field_value(code);
        return default_value(code, field.substr(last + 1));
    }
    bool optional_field(const std::string& field) {
        size_t colon = field.rfind(':');
        if (colon == std::string::npos) return false;
        std::string code = field.substr(1, colon - 1);
        if (fits(code)) return field_value(code);
        return default_value(code, field.substr(colon + 1));
    }

    bool field_value(const std::string& field) {
        if (field == "p") {
            const AcisToken* t = peek();
            if (!t || t->type != AcisTokenType::Pointer) return false;
            pointer(static_cast<int32_t>(t->number));
            ++pos_;
            return true;
        }
        if (field == "t") {
            if (version_ >= 700) return integer_field();
            out_.integer(-1);
            return true;
        }
        if (field == "n") {
            if (version_ >= 700) return field_value("p");
            out_.pointer(-1);
            return true;
        }
        if (field == "i") return integer_field();
        if (field == "q") {
            long long v = 0;
            if (!integer_value(v)) return false;
            out_.integer64(v);
            return true;
        }
        if (field == "d") {
            double v = 0.0;
            if (!real_value(v)) return false;
            out_.real(v);
            return true;
        }
        if (field == "P" || field == "V") {
            double v[3];
            for (double& x : v) {
                if (!real_value(x)) return false;
            }
            out_.triple(field == "P", v);
            last_vector_length_ = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            return true;
        }
        if (field == "S") {
            std::string s;
            if (!string_value(s)) return false;
            out_.string(s);
            last_string_ = s;
            return true;
        }
        if (field == "I") return interval_end();
        if (field == "w") return identifier();
        if (field == "c") {
            int32_t v = 0;
            if (version_ >= 21800 && !int32_value(v)) return false;
            out_.integer(v);
            return true;
        }
        if (field == "a") {
            if (at_type(AcisTokenType::Number)) return real_field();
            return edge_parameters();
        }
        if (field == "b") {
            if (at_type(AcisTokenType::Number)) return real_field();
            if (!have_edge_end_) return false;
            out_.real(edge_end_);
            return true;
        }
        // The cone's u scale: the major axis' length (the last vector before it)
        if (field == "k") {
            if (at_type(AcisTokenType::Number)) return real_field();
            out_.real(last_vector_length_);
            return true;
        }
        if (field == "G") return coedge_curve();
        if (field == "*p") {
            while (at_type(AcisTokenType::Pointer)) {
                pointer(static_cast<int32_t>(peek()->number));
                ++pos_;
            }
            return true;
        }
        if (field == "*i") {
            while (peek()) {
                if (!integer_field()) return false;
            }
            return true;
        }
        if (field == "R") return eye_fields();
        if (field == "B") return bs_curve(3);
        if (field == "B2") return bs_curve(2);
        if (field == "N") return bs_surface();
        if (field == "D") return discontinuities();
        if (field == "Q") return run(int_cur_data);
        if (field == "K") return curve_approximation();
        if (field == "Y") return spl_data();
        if (field == "C") return inline_geometry("curve");
        if (field == "U") return inline_geometry("surface");
        if (field == "X") return unknown_data();
        if (field == "A") return custom_data();
        if (field.rfind("T/", 0) == 0) return subtype(field.substr(2));
        if (field.rfind("~", 0) == 0) return later_field(field);
        if (field.rfind("?", 0) == 0) return optional_field(field);
        if (field.rfind("L?/", 0) == 0) return logical(field, true);
        if (field.rfind("L/", 0) == 0) return logical(field, false);
        if (field.rfind("E/", 0) == 0) return enumeration(field.substr(2));
        return false;
    }

    SabContext& ctx_;
    const AcisRecord& record_;
    size_t index_;
    int version_;
    const std::string& sat_;
    SabWriter& out_;
    size_t pos_ = 0;
    bool copy_ = false;
    int depth_ = 0;
    std::string last_string_;
    std::string special_error_;
    SabSplineData last_spline_;
    double last_vector_length_ = 0.0;
    double edge_end_ = 0.0;
    bool have_edge_end_ = false;
};

// A curve's knots, as a bs3's whole knot vector has them, by value: (value, multiplicity), the
// first and the last of the whole vector left out as ACIS does
std::vector<std::pair<double, int>> knot_runs(const std::vector<double>& whole) {
    std::vector<std::pair<double, int>> runs;
    for (size_t i = 1; i + 1 < whole.size(); ++i) {
        if (!runs.empty() && whole[i] == runs.back().first) ++runs.back().second;
        else runs.push_back({whole[i], 1});
    }
    return runs;
}

// The order of the discontinuity at a knot of multiplicity m of a degree p spline (1, 2, 3; 0
// none): the first derivative that may jump there
int discontinuity_order(int degree, int multiplicity) {
    int order = std::max(1, degree - multiplicity + 1);
    return order <= 3 ? order : 0;
}

// A tolerant coedge's 3D curve when the SAT has none (SAT 600): its pcurve on its surface, as
// AutoCAD's chain writes it — an intcurve "i1 intcurve forward { par_int_cur 23100 full … }",
// the curve in the coedge's parameters, which are the pcurve's (negated when the pcurve is
// reversed). Its bs3 is cubic, a Bézier piece (multiplicity 3) between the breaks — the
// pcurve's knots and where the pcurve crosses a knot line of its surface — each with the curve's
// ends and their tangents (cubic Hermite; the chain's pieces have them, but a piece it splits
// or joins where its fit allows); the fit tolerance the largest distance between them at the
// quarters of the pieces. Then the surface, null_surface, the pcurve's bs2 (in the coedge's
// parameters), nullbs, the infinite range, the discontinuities (the pcurve's knots and those
// crossings, by multiplicity as for SAT 106's bs3, see spline_discontinuities()), 0, T, F.
bool RecordEncoder::computed_coedge_curve() {
    const AcisFile& file = *ctx_.file;
    const auto& t = record_.tokens;
    size_t ds = version_ >= 700 ? 3 : 1;
    auto fail = [&](const std::string& why) {
        special_error_ = "the ACIS record " + std::to_string(index_) + " (\"" + record_.type + "\") " + why;
        return false;
    };
    if (t.size() < ds + 9 || t[ds + 6].type != AcisTokenType::Pointer || t[ds + 6].number < 0) {
        return fail("has no pcurve for its 3D curve");
    }
    size_t pcurve = static_cast<size_t>(t[ds + 6].number);
    AcisSplineCurve uv;
    AcisSurface surface;
    bool pcurve_reversed = false;
    double offset[2] = {0.0, 0.0};
    if (!acis_pcurve(file, pcurve, uv, surface, pcurve_reversed, offset)) {
        return fail("has a pcurve whose curve cannot be made its 3D curve");
    }
    // The pcurve is in the coedge's parameters (speaker's reversed coedges have pcurves from -1 to
    // 0, as the chain's par_int_cur); a reversed pcurve's parameter is negated
    bool flip = pcurve_reversed;
    double lo = uv.knots[static_cast<size_t>(uv.degree)];
    double hi = uv.knots[uv.points.size()];
    auto uv_at = [&](double x) {
        Point3 q;
        acis_spline_point(uv, flip ? -x : x, q);
        return Point3{q.x + offset[0], q.y + offset[1], 0.0};
    };
    bool good = true;
    auto point = [&](double x) {
        Point3 q = uv_at(x);
        Point3 p;
        if (!acis_surface_point(surface, q.x, q.y, p)) good = false;
        return p;
    };
    point(flip ? -hi : lo);
    if (!good) return fail("has a pcurve on a surface without parameters");

    // The breaks: the pcurve's knots, then the surface's knot lines it crosses
    std::vector<std::pair<double, int>> breaks;          // parameter, discontinuity order
    for (const auto& k : knot_runs(uv.knots)) {
        breaks.push_back({flip ? -k.first : k.first, discontinuity_order(uv.degree, k.second)});
    }
    std::sort(breaks.begin(), breaks.end());
    std::vector<SabKnotLine> lines;
    auto add_knots = [&](int coordinate, const std::vector<double>& whole, int degree, double sign) {
        auto runs = knot_runs(whole);
        for (size_t i = 1; i + 1 < runs.size(); ++i) {
            lines.push_back({coordinate, sign * runs[i].first, discontinuity_order(degree, runs[i].second)});
        }
    };
    if (surface.type == AcisSurfaceType::Spline) {
        add_knots(0, surface.spline.knots_u, surface.spline.degree_u, 1.0);
        add_knots(1, surface.spline.knots_v, surface.spline.degree_v, 1.0);
    } else if (surface.type == AcisSurfaceType::Revolution || surface.type == AcisSurfaceType::Sum) {
        for (size_t c = 0; c < surface.curves.size() && c < 2; ++c) {
            const AcisCurve& curve = surface.curves[c];
            if (curve.type != AcisCurveType::Spline) continue;
            add_knots(static_cast<int>(c), curve.spline.knots, curve.spline.degree, curve.reversed ? -1.0 : 1.0);
        }
    }
    std::vector<std::pair<double, int>> crossings;
    for (size_t b = 0; b + 1 < breaks.size(); ++b) {
        double a0 = breaks[b].first, a1 = breaks[b + 1].first;
        for (const SabKnotLine& l : lines) {
            auto f = [&](double x) {
                Point3 q = uv_at(x);
                return (l.coordinate == 0 ? q.x : q.y) - l.value;
            };
            const int samples = 16;
            double x0 = a0, f0 = f(a0);
            for (int s = 1; s <= samples; ++s) {
                double x1 = a0 + (a1 - a0) * s / samples, f1 = f(x1);
                if (f0 != 0.0 && f1 != 0.0 && (f0 < 0.0) != (f1 < 0.0)) {
                    double ya = x0, yb = x1, fa = f0;
                    for (int it = 0; it < 80; ++it) {
                        double m = 0.5 * (ya + yb), fm = f(m);
                        if ((fm < 0.0) == (fa < 0.0)) {
                            ya = m;
                            fa = fm;
                        } else {
                            yb = m;
                        }
                    }
                    crossings.push_back({0.5 * (ya + yb), l.order});
                }
                x0 = x1;
                f0 = f1;
            }
        }
    }
    for (const auto& c : crossings) {
        bool near = false;
        for (const auto& b : breaks) near = near || std::fabs(b.first - c.first) <= 1e-12 * (1.0 + std::fabs(c.first));
        if (!near) breaks.push_back(c);
    }
    std::sort(breaks.begin(), breaks.end());

    // The Bézier pieces: the ends and their tangents inside the piece (the surface's derivatives
    // along the pcurve's, a hair inside the piece's ends where the pcurve may turn)
    auto tangent = [&](double x) {
        Point3 q, dq;
        acis_spline_point(uv, flip ? -x : x, q, &dq);
        if (flip) dq = Point3{-dq.x, -dq.y, 0.0};
        Point3 p, su, sv;
        if (!acis_surface_point(surface, q.x + offset[0], q.y + offset[1], p, &su, &sv)) good = false;
        return Point3{su.x * dq.x + sv.x * dq.y, su.y * dq.x + sv.y * dq.y, su.z * dq.x + sv.z * dq.y};
    };
    std::vector<Point3> points;
    double fit = 0.0, size = 0.0;
    for (size_t b = 0; b + 1 < breaks.size(); ++b) {
        double a0 = breaks[b].first, a1 = breaks[b + 1].first, len = a1 - a0, hair = len * 1e-13;
        Point3 p0 = point(a0), p3 = point(a1);
        Point3 d0 = tangent(a0 + hair), d1 = tangent(a1 - hair);
        Point3 p1{p0.x + d0.x * len / 3.0, p0.y + d0.y * len / 3.0, p0.z + d0.z * len / 3.0};
        Point3 p2{p3.x - d1.x * len / 3.0, p3.y - d1.y * len / 3.0, p3.z - d1.z * len / 3.0};
        if (b == 0) points.push_back(p0);
        points.push_back(p1);
        points.push_back(p2);
        points.push_back(p3);
        for (double s : {0.25, 0.5, 0.75}) {
            double w0 = (1 - s) * (1 - s) * (1 - s), w1 = 3 * s * (1 - s) * (1 - s), w2 = 3 * s * s * (1 - s), w3 = s * s * s;
            Point3 z = point(a0 + s * len);
            double dx = w0 * p0.x + w1 * p1.x + w2 * p2.x + w3 * p3.x - z.x;
            double dy = w0 * p0.y + w1 * p1.y + w2 * p2.y + w3 * p3.y - z.y;
            double dz = w0 * p0.z + w1 * p1.z + w2 * p2.z + w3 * p3.z - z.z;
            fit = std::max(fit, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        size = std::max({size, std::fabs(p0.x), std::fabs(p0.y), std::fabs(p0.z)});
    }
    if (!good || breaks.size() < 2) return fail("has a pcurve whose curve cannot be made its 3D curve");
    // An exact piece (a line on a cylinder): the chain's fit tolerance 0
    if (fit <= 1e-12 * (1.0 + size)) fit = 0.0;

    // Where the pcurve's definition and its surface are: "{ exp_par_cur bs2 fit surface }"
    const AcisRecord& pr = file.records[pcurve];
    size_t at = 0;
    while (at < pr.tokens.size() && pr.tokens[at].type != AcisTokenType::SubtypeStart) ++at;
    size_t def_record = pcurve, def_token = at;
    if (at + 2 < pr.tokens.size() && pr.tokens[at + 1].type == AcisTokenType::Word && pr.tokens[at + 1].text == "ref") {
        size_t k = static_cast<size_t>(pr.tokens[at + 2].number);
        if (k >= file.subtypes.size()) return fail("has a pcurve whose curve cannot be made its 3D curve");
        def_record = file.subtypes[k].record;
        def_token = file.subtypes[k].token;
    }
    std::vector<uint8_t> scratch;
    SabWriter discard(scratch);
    RecordEncoder probe(ctx_, def_record, discard, def_token + 2, true);
    if (!probe.field_value("B2") || !probe.field_value("d")) return fail("has a pcurve whose curve cannot be made its 3D curve");

    out_.integer(1);
    out_.identifier("intcurve");
    out_.logical(false);
    out_.subtype_start();
    ++ctx_.subtypes;
    out_.identifier("par_int_cur");
    out_.integer(23100);
    out_.enumeration(0);
    // The bs3 curve
    out_.identifier("nubs");
    out_.integer(3);
    out_.enumeration(0);
    out_.integer(static_cast<int32_t>(breaks.size()));
    for (const auto& b : breaks) {
        out_.real(b.first);
        out_.integer(3);
    }
    for (const Point3& p : points) {
        double v[3] = {p.x, p.y, p.z};
        for (double x : v) out_.real(x);
    }
    out_.real(fit);
    RecordEncoder copy(ctx_, def_record, out_, probe.pos_, true);
    copy.depth_ = depth_ + 1;
    if (!copy.inline_geometry("surface")) {
        special_error_ = copy.special_error_;
        return special_error_.empty() ? fail("has a pcurve whose surface cannot be written") : false;
    }
    out_.identifier("null_surface");
    // The bs2 curve in the coedge's parameters
    bool rational = !uv.weights.empty();
    auto runs = knot_runs(uv.knots);
    out_.identifier(rational ? "nurbs" : "nubs");
    out_.integer(uv.degree);
    out_.enumeration(uv.periodic ? 2 : 0);
    out_.integer(static_cast<int32_t>(runs.size()));
    for (size_t i = 0; i < runs.size(); ++i) {
        const auto& k = flip ? runs[runs.size() - 1 - i] : runs[i];
        out_.real(flip ? -k.first : k.first);
        out_.integer(k.second);
    }
    for (size_t i = 0; i < uv.points.size(); ++i) {
        size_t j = flip ? uv.points.size() - 1 - i : i;
        out_.real(uv.points[j].x);
        out_.real(uv.points[j].y);
        if (rational) out_.real(uv.weights[j]);
    }
    out_.identifier("nullbs");
    out_.logical(false);
    out_.logical(false);
    std::vector<double> lists[3];
    for (const auto& b : breaks) {
        if (b.first <= breaks.front().first || b.first >= breaks.back().first) continue;
        if (b.second >= 1 && b.second <= 3) lists[b.second - 1].push_back(b.first);
    }
    for (const auto& l : lists) {
        out_.integer(static_cast<int32_t>(l.size()));
        for (double x : l) out_.real(x);
    }
    out_.integer(0);
    out_.logical(true);
    out_.logical(false);
    out_.subtype_end();
    out_.logical(false);
    out_.logical(false);
    return true;
}

} // namespace

bool sab_from_sat(const std::string& sat, std::vector<uint8_t>& out, std::string& error, int format) {
    out.clear();
    error.clear();
    SatHeader head;
    AcisFile file;
    if (format != sab_format_acis && format != sab_format_asm) {
        error = "the SAB format " + std::to_string(format) + " is not 21800 or 22300";
        return false;
    }
    if (!read_sat(sat, file)) {
        error = "the ACIS data cannot be read as SAT";
        return false;
    }
    // The SAT ends with a record's "#" or its end record ("End-of-ASM-data", "End-of-ACIS-data"; AutoCAD's
    // 2000 and 2004 SAT has none): read_sat() takes the records before a cut, and a SAB of part of a body
    // is never made
    const char* const blank = " \t\r\n";
    size_t last = sat.find_last_not_of(blank);
    // A "#" separates records as well as a blank does ("… #End-of-ACIS-data" is a whole SAT)
    size_t before = last == std::string::npos ? std::string::npos : sat.find_last_of(" \t\r\n#", last);
    size_t word = before == std::string::npos ? 0 : before + 1;
    bool ended = last != std::string::npos && (sat[last] == '#' || sat.compare(word, 7, "End-of-") == 0);
    if (!ended) {
        error = "the ACIS data is cut off: its last record has no end";
        return false;
    }
    head.version = file.version;
    if (head.version > 21800 || head.version < 100) {
        error = "ACIS data of version " + std::to_string(head.version) + " cannot be written as " + target_name(format);
        return false;
    }
    if (!read_header(sat, head)) {
        error = "the ACIS data cannot be read as SAT";
        return false;
    }

    SabContext ctx;
    ctx.file = &file;
    ctx.sat = &sat;
    ctx.version = head.version;
    ctx.format = format;
    for (size_t k = 0; k < file.subtypes.size(); ++k) {
        ctx.definitions[{file.subtypes[k].record, file.subtypes[k].token}] = k;
    }
    ctx.subtype_number.assign(file.subtypes.size(), -1);
    // The SAT before 20800 has no asmheader: it is put in front, and every pointer moves on
    // by one (the chain's SAB of SAT 106 … 700, B63)
    bool asmheader = !file.records.empty() && file.records[0].type == "asmheader";
    ctx.shift = asmheader ? 0 : 1;

    SabWriter w(out);
    // AutoCAD's head: the signature, the format version, records (0), entities (2) and the flags
    // (12 in 21800's "ACIS BinaryFile", 4 in 22300's "ASM BinaryFile4")
    const char magic[] = "ACIS BinaryFile";
    const char magic_asm[] = "ASM BinaryFile4";
    if (format == sab_format_asm) w.raw_bytes(magic_asm, sizeof magic_asm - 1);
    else w.raw_bytes(magic, sizeof magic - 1);
    w.raw_int32(format);
    w.raw_int32(0);
    w.raw_int32(2);
    w.raw_int32(format == sab_format_asm ? 4 : 12);
    w.string(head.product);
    w.string(head.acis_version);
    w.string(head.date);
    w.real(head.units);
    w.real(head.resabs);
    w.real(head.resnor);

    if (!asmheader) {
        // The ASM version of the SAT's "ASM 230.7.0.65535 NT", else ODA's and ezdxf's
        std::string version = default_asm_version;
        if (head.acis_version.rfind("ASM ", 0) == 0) {
            size_t e = head.acis_version.find(' ', 4);
            version = head.acis_version.substr(4, e == std::string::npos ? std::string::npos : e - 4);
        }
        w.name("asmheader");
        w.pointer(-1);
        w.integer(-1);
        w.string(version);
        w.end_record();
    }
    for (size_t i = 0; i < file.records.size(); ++i) {
        const AcisRecord& r = file.records[i];
        const char* fields = nullptr;
        for (const auto& l : field_lists) {
            if (r.type == l.type) fields = l.fields;
        }
        if (!fields) {
            if (r.type.empty()) error = "the ACIS record " + std::to_string(i) + " is missing";
            else error = "the ACIS record \"" + r.type + "\" cannot be written as " + target_name(format);
            out.clear();
            return false;
        }
        w.name(r.type);
        RecordEncoder record(ctx, i, w);
        if (!record.encode(fields, error)) {
            out.clear();
            return false;
        }
    }
    w.name("End-of-ASM-data");
    return true;
}

} // namespace internal
} // namespace hdxf
