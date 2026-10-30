#pragma once

#include "test_framework.hpp"
#include "dimension_block.hpp"
#include "handles.hpp"
#include "group_io.hpp"
#include "hdxf.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// The structure every DXF 2000 file HDxf writes must have (plan phase 1, 1.2 and 1.4):
// unique handles, every pointer to a record that exists, $HANDSEED above all handles,
// owners before what they own in OBJECTS, LAYOUT <-> BLOCK_RECORD. ODA File Converter and
// ezdxf accept files that break these, so the tests check them themselves.

namespace dxf_structure {

inline std::string upper_copy_of(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// An integer's text without the alignment AutoCAD (and HDxf) give it in ASCII ("     0"
// is 0), so the tests compare values
inline std::string number_text(int code, std::string v) {
    using hdxf::internal::GroupValueType;
    GroupValueType t = hdxf::internal::group_value_type(code);
    if (t == GroupValueType::Int16 || t == GroupValueType::Int32 || t == GroupValueType::Int64 ||
        t == GroupValueType::Bool) {
        size_t i = v.find_first_not_of(' ');
        v = i == std::string::npos ? std::string() : v.substr(i);
    }
    return v;
}

using Group = std::pair<int, std::string>;

struct Rec {
    std::string section;
    std::vector<Group> groups;          // groups[0] is the 0 group

    const std::string& type() const { return groups[0].second; }
    std::string value(int code) const {
        for (const auto& g : groups) if (g.first == code) return g.second;
        return {};
    }
};

inline std::vector<Rec> records_of(const std::vector<uint8_t>& data) {
    std::vector<std::string> lines;
    std::string cur;
    for (uint8_t c : data) {
        if (c == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            lines.push_back(cur);
            cur.clear();
        } else {
            cur += static_cast<char>(c);
        }
    }
    std::vector<Rec> out;
    std::string section;
    for (size_t i = 0; i + 1 < lines.size(); i += 2) {
        int code = std::atoi(lines[i].c_str());
        const std::string& value = lines[i + 1];
        if (code == 0) out.push_back({section, {}});
        if (out.empty()) continue;
        out.back().groups.push_back({code, value});
        if (code == 2 && out.back().type() == "SECTION") section = value;
        if (code == 0 && value == "ENDSEC") section.clear();
    }
    return out;
}

// The records of an ASCII or binary file (the group reader of the library; numbers without their alignment)
inline std::vector<Rec> records_of_file(const std::vector<uint8_t>& data) {
    std::istringstream in(std::string(data.begin(), data.end()), std::ios::binary);
    hdxf::internal::GroupReader reader(in);
    reader.detect_format();
    std::vector<Rec> out;
    std::string section;
    hdxf::internal::GroupCode gc;
    while (reader.read(gc)) {
        std::string value = number_text(gc.code, gc.value);
        if (gc.code == 0) out.push_back({section, {}});
        if (out.empty()) continue;
        out.back().groups.push_back({gc.code, value});
        if (gc.code == 2 && out.back().type() == "SECTION") section = value;
        if (gc.code == 0 && value == "ENDSEC") section.clear();
    }
    hdxf_test::assert_true(reader.error().empty(), "reading the file: " + reader.error());
    return out;
}

inline uint64_t handle_value(const std::string& h) {
    uint64_t v = 0;
    hdxf_test::assert_true(hdxf::internal::parse_handle(h, v) == hdxf::internal::HandleParse::Ok,
                           "not a handle: \"" + h + "\"");
    return v;
}

// The checks every DXF 2000 file HDxf writes must pass. `tolerated`: pointers the file read had to a record that
// is not there (a file of another program can have them; B19 keeps them and does not make new ones), as
// dangling_pointers() lists them
inline void check_records(std::vector<Rec> recs, const std::string& what, const std::set<std::string>* tolerated = nullptr);

inline void check_structure(const std::vector<uint8_t>& data, const std::string& what) {
    check_records(records_of(data), what);
}

// Whether a code is a pointer to a record of the file
inline bool is_pointer_code(int code) {
    return code == 330 || code == 331 || code == 340 || code == 342 || code == 343 || code == 344 || code == 350 ||
           code == 360 || code == 390;
}

// The pointers of a file's records to a handle no record has: "TYPE <handle> <code> <value>"
inline std::set<std::string> dangling_pointers(const std::vector<Rec>& recs) {
    std::set<std::string> handles;
    for (const auto& r : recs) {
        const std::string& t = r.type();
        if (t == "SECTION" || t == "ENDSEC" || t == "ENDTAB" || t == "EOF" || t == "CLASS" || r.section == "ACDSDATA") continue;
        const std::string h = r.value(t == "DIMSTYLE" ? 105 : 5);
        if (!h.empty()) handles.insert(upper_copy_of(h));
    }
    std::set<std::string> out;
    // A $HANDSEED not above the file's handles is as the file has it: kept while nothing is written anew
    uint64_t max_handle = 0;
    for (const auto& h : handles) max_handle = std::max<uint64_t>(max_handle, std::strtoull(h.c_str(), nullptr, 16));
    for (const auto& r : recs) {
        if (r.type() != "SECTION" || r.value(2) != "HEADER") continue;
        for (size_t i = 0; i + 1 < r.groups.size(); ++i) {
            if (r.groups[i] == Group{9, "$HANDSEED"} && std::strtoull(r.groups[i + 1].second.c_str(), nullptr, 16) <= max_handle) {
                out.insert("$HANDSEED");
            }
        }
    }
    for (const auto& r : recs) {
        const std::string& t = r.type();
        if (t == "SECTION" || t == "ENDSEC" || t == "ENDTAB" || t == "EOF" || t == "CLASS" || r.section == "ACDSDATA") continue;
        const std::string self = r.value(t == "DIMSTYLE" ? 105 : 5);
        for (const auto& [code, value] : r.groups) {
            if (code >= 1000) break;
            if (!is_pointer_code(code) || value == "0" || value.empty()) continue;
            if (handles.count(upper_copy_of(value)) == 0) out.insert(t + " " + self + " " + std::to_string(code) + " " + value);
        }
    }
    return out;
}

inline void check_records(std::vector<Rec> recs, const std::string& what, const std::set<std::string>* tolerated) {
    std::map<uint64_t, const Rec*> by_handle;
    std::string seed;

    for (auto& r : recs) {
        // HEADER is one record: 0 SECTION, 2 HEADER and all the variables
        if (r.type() == "SECTION" && r.value(2) == "HEADER") {
            r.section = "HEADER";
            for (size_t i = 0; i + 1 < r.groups.size(); ++i) {
                if (r.groups[i] == Group{9, "$HANDSEED"}) seed = r.groups[i + 1].second;
            }
            continue;
        }
        const std::string& t = r.type();
        if (t == "SECTION" || t == "ENDSEC" || t == "ENDTAB" || t == "EOF" || t == "CLASS") continue;
        if (r.section == "ACDSDATA") continue;     // 2013+: schemas and records have no handle
        std::string h = r.value(t == "DIMSTYLE" ? 105 : 5);
        hdxf_test::assert_false(h.empty(), what + ": " + t + " without handle");
        hdxf_test::assert_true(by_handle.emplace(handle_value(h), &r).second,
                               what + ": handle " + h + " used twice");
    }
    hdxf_test::assert_false(seed.empty(), what + ": no $HANDSEED");
    uint64_t max = by_handle.empty() ? 0 : by_handle.rbegin()->first;
    // (a file read keeps the $HANDSEED it has while nothing changes, also one below the handles a writer left in it:
    // the file as it was, B12 — "$HANDSEED" is in the tolerated set then)
    const bool seed_as_read = tolerated && tolerated->count("$HANDSEED") > 0;
    hdxf_test::assert_true(seed_as_read || handle_value(seed) > max, what + ": $HANDSEED " + seed + " not above all handles");

    std::set<uint64_t> written;
    for (const auto& r : recs) {
        const std::string& t = r.type();
        if (r.section == "HEADER" || t == "SECTION" || t == "ENDSEC" || t == "ENDTAB" ||
            t == "EOF" || t == "CLASS") continue;
        // ACDSDATA (2013+): a record's 320 is the record it belongs to; its schema (90) is one
        // of the section's
        if (r.section == "ACDSDATA") {
            if (t != "ACDSRECORD") continue;
            std::string owner = r.value(320);
            hdxf_test::assert_true(by_handle.count(handle_value(owner)) > 0,
                                   what + ": ACDSRECORD of " + owner + ", which does not exist");
            int schemas = 0;
            for (const auto& s2 : recs) schemas += s2.section == "ACDSDATA" && s2.type() == "ACDSSCHEMA";
            int schema = std::atoi(r.value(90).c_str());
            hdxf_test::assert_true(schema >= 0 && schema < schemas, what + ": ACDSRECORD of schema " + r.value(90));
            continue;
        }
        std::string self = r.value(t == "DIMSTYLE" ? 105 : 5);
        bool in_head = true;            // before the first subclass marker
        bool in_102 = false;
        bool owner_seen = false;
        for (const auto& [code, value] : r.groups) {
            if (code == 100) in_head = false;
            if (code == 102) { in_102 = !value.empty() && value[0] == '{'; continue; }
            bool owner = in_head && !in_102 && code == 330 && !owner_seen;
            if (owner) owner_seen = true;
            bool pointer = is_pointer_code(code);
            if (!pointer) continue;
            // 0 is "none" for a pointer (AutoCAD writes it, e.g. WIPEOUT 340); as an owner
            // only the TABLE records and the root dictionary have none
            if (value == "0") {
                hdxf_test::assert_true(!owner || t == "TABLE" || t == "DICTIONARY",
                                       what + ": " + t + " " + self + " has no owner");
                continue;
            }
            if (tolerated && tolerated->count(t + " " + self + " " + std::to_string(code) + " " + value) > 0) continue;
            hdxf_test::assert_true(by_handle.count(handle_value(value)) > 0,
                                   what + ": " + t + " " + self + " points to " + value +
                                   " (" + std::to_string(code) + "), which does not exist");
        }
        // In OBJECTS the owner comes first (DXF reference, OBJECTS section)
        if (r.section == "OBJECTS") {
            std::string owner;
            bool in_102 = false;
            for (const auto& [code, value] : r.groups) {
                if (code == 102) { in_102 = value[0] == '{'; continue; }
                if (code == 330 && !in_102) { owner = value; break; }
            }
            // (an extension dictionary's owner is an entity or a table entry, earlier anyway)
            // (none in R13/R14 family N, which has the owner only as a reactor: format profile D1)
            auto o = owner.empty() ? by_handle.end() : by_handle.find(handle_value(owner));
            if (owner != "0" && o != by_handle.end() && o->second->section == "OBJECTS") {
                hdxf_test::assert_true(written.count(handle_value(owner)) > 0,
                                       what + ": " + t + " " + self + " comes before its owner");
            }
            written.insert(handle_value(self));
        }
    }

    // LAYOUT 330 (after AcDbLayout) → BLOCK_RECORD whose 340 is that LAYOUT
    for (const auto& r : recs) {
        if (r.type() != "LAYOUT") continue;
        std::string record;
        bool in_layout = false;
        for (const auto& [code, value] : r.groups) {
            if (code == 100) in_layout = value == "AcDbLayout";
            if (in_layout && code == 330) record = value;
        }
        const Rec* br = by_handle[handle_value(record)];
        hdxf_test::assert_eq(br->type(), "BLOCK_RECORD", what + ": LAYOUT → BLOCK_RECORD");
        hdxf_test::assert_eq(br->value(340), r.value(5), what + ": BLOCK_RECORD → LAYOUT");
    }
}

// What an R13 or R14 file HDxf writes must not have besides the checks above (plan phase 7, 7.22, profile 5.3, 9):
// no owner 330 outside a {...} group unless the file is of family A2000 (`owner_groups`, a drawing read from such a
// file), none of 2000's groups (370 380 390 410 in entities, 91 in CLASS, 280 281 in DICTIONARY, 280 in XRECORD) and
// no LAYOUT or PLOTSETTINGS — unless the file was read and had them (`original`: the records of that file; the
// helper graph AutoCAD of the 2000 family left in it is kept, never made: at most as many as it had)
inline void check_older_records(const std::vector<Rec>& recs, bool owner_groups, const std::string& what,
                                const std::vector<Rec>* original = nullptr) {
    std::set<std::string> tolerated;
    if (original) tolerated = dangling_pointers(*original);
    check_records(recs, what, &tolerated);
    std::map<std::string, int> allowed_native;
    if (original) {
        for (const auto& r : *original) {
            if (r.type() == "LAYOUT" || r.type() == "PLOTSETTINGS") ++allowed_native[r.type()];
        }
    }
    for (const auto& r : recs) {
        const std::string& t = r.type();
        if (r.section == "HEADER" || t == "SECTION" || t == "ENDSEC" || t == "EOF") continue;
        if (r.section == "CLASSES") {
            hdxf_test::assert_true(r.value(91).empty(), what + ": 91 in a CLASS");
            continue;
        }
        bool in_list = false;
        bool xdata = false;
        bool in_subclass = false;
        for (const auto& [code, value] : r.groups) {
            if (code == 102) { in_list = !value.empty() && value[0] == '{'; continue; }
            if (code >= 1000) xdata = true;
            if (xdata) continue;
            if (code == 100) in_subclass = true;
            if (code == 330 && !in_list && !owner_groups) {
                // (a pointer in a subclass of its own: HATCH/LEADER/IMAGE links and the like are 330 in 2000+ too; the
                // ones allowed outside the head are listed by type)
                bool link = t == "XRECORD" || t == "IMAGEDEF_REACTOR" || t == "HATCH" || t == "LEADER" || t == "LAYOUT" ||
                            t == "MLINE" || t == "MULTILEADER" || t == "VIEWPORT" || t == "IMAGE" || t == "ACDBDICTIONARYWDFLT";
                hdxf_test::assert_true(link && in_subclass,
                                       what + ": owner 330 " + value + " in a " + t + " of family N");
            }
            if (r.section == "ENTITIES" || r.section == "BLOCKS") {
                hdxf_test::assert_true(code != 370 && code != 380 && code != 390 && code != 410 && code != 440,
                                       what + ": " + std::to_string(code) + " in a " + t);
            }
            // (a file of family A2000 keeps the 280 and 281 its dictionaries and XRECORDs were read with)
            if (t == "DICTIONARY" && !owner_groups) {
                hdxf_test::assert_true(code != 280 && code != 281, what + ": " + std::to_string(code) + " in a DICTIONARY");
            }
            if (t == "XRECORD" && code == 280 && !owner_groups) hdxf_test::assert_true(false, what + ": 280 in an XRECORD");
        }
        if (t == "LAYOUT" || t == "PLOTSETTINGS") {
            hdxf_test::assert_true(allowed_native[t]-- > 0, what + ": " + t + " in an R13/R14 file");
        }
    }
}

// ---------------------------------------------------------------------------
// Names a file refers to: every one is the name of a record there, spelled as there (plan phase 7, 7.22; before
// that a helper of test_integration_r13_r14.cpp)
// ---------------------------------------------------------------------------

struct Symbols {
    std::set<std::string> layers, linetypes, styles, dim_styles, blocks;
};

inline Symbols symbols_of(const std::vector<Rec>& recs) {
    Symbols s;
    for (const auto& r : recs) {
        if (r.section != "TABLES") continue;
        if (r.type() == "LAYER") s.layers.insert(r.value(2));
        else if (r.type() == "LTYPE") s.linetypes.insert(r.value(2));
        else if (r.type() == "STYLE") s.styles.insert(r.value(2));
        else if (r.type() == "DIMSTYLE") s.dim_styles.insert(r.value(2));
        else if (r.type() == "BLOCK_RECORD") s.blocks.insert(r.value(2));
    }
    return s;
}

// The first value of a code before the XDATA
inline std::string head_value(const Rec& r, int code) {
    for (const auto& g : r.groups) {
        if (g.first >= 1000) break;
        if (g.first == code) return g.second;
    }
    return {};
}

inline void expect_in(const std::set<std::string>& table, const std::string& name, const std::string& what) {
    hdxf_test::assert_true(table.count(name) > 0, what + ": \"" + name + "\" has no record of that spelling");
}

// The value of the first code after the subclass marker `100 <marker>`
inline std::string value_after_subclass(const Rec& r, const std::string& marker, int code) {
    bool in = false;
    for (const auto& g : r.groups) {
        if (g.first == 100) in = g.second == marker;
        else if (in && g.first == code) return g.second;
    }
    return {};
}

// `exact`: a name is spelled as the record has it (a drawing written here); false: AutoCAD's own rule, the case of
// a name does not matter (a file read may spell a name another way than its record)
inline void check_references(const std::vector<Rec>& recs, const std::string& what, bool exact = true) {
    Symbols s = symbols_of(recs);
    if (!exact) {
        for (auto* table : {&s.layers, &s.linetypes, &s.styles, &s.dim_styles, &s.blocks}) {
            std::set<std::string> upper_names;
            for (const auto& n : *table) upper_names.insert(upper_copy_of(n));
            *table = upper_names;
        }
    }
    auto expect = [&](const std::set<std::string>& table, const std::string& name, const std::string& where) {
        // (AutoCAD's own Test3_FourPlusUnits_r14.dxf has $DIMSTYLE empty: a file read may have no name)
        if (!exact && name.empty()) return;
        expect_in(table, exact ? name : upper_copy_of(name), where);
    };
    // the header
    for (const auto& r : recs) {
        if (r.type() != "SECTION" || r.value(2) != "HEADER") continue;
        for (size_t i = 0; i + 1 < r.groups.size(); ++i) {
            if (r.groups[i].first != 9) continue;
            const std::string& variable = r.groups[i].second;
            const std::string& value = r.groups[i + 1].second;
            if (variable == "$CLAYER") expect(s.layers, value, what + ": $CLAYER");
            if (variable == "$CELTYPE") expect(s.linetypes, value, what + ": $CELTYPE");
            if (variable == "$TEXTSTYLE" || variable == "$DIMTXSTY") expect(s.styles, value, what + ": " + variable);
            if (variable == "$DIMSTYLE") expect(s.dim_styles, value, what + ": $DIMSTYLE");
            if ((variable == "$DIMBLK" || variable == "$DIMBLK1" || variable == "$DIMBLK2") && !value.empty()) {
                expect(s.blocks, value, what + ": " + variable);
            }
        }
    }
    for (const auto& r : recs) {
        const std::string& t = r.type();
        if (r.section == "TABLES" && t == "LAYER") expect(s.linetypes, head_value(r, 6), what + ": linetype of layer " + r.value(2));
        if ((r.section != "ENTITIES" && r.section != "BLOCKS") || t == "ENDSEC") continue;
        const std::string place = what + ": " + t + " " + r.value(5);
        if (t != "ENDBLK" && t != "SEQEND") expect(s.layers, head_value(r, 8), place + " layer");
        const std::string linetype = head_value(r, 6);
        if (!linetype.empty()) expect(s.linetypes, linetype, place + " linetype");
        if (t == "TEXT" || t == "ATTRIB" || t == "ATTDEF" || t == "MTEXT") {
            const std::string style = head_value(r, 7);
            if (!style.empty()) expect(s.styles, style, place + " style");
        }
        if (t == "DIMENSION" || t == "LEADER" || t == "TOLERANCE") expect(s.dim_styles, head_value(r, 3), place + " dimension style");
        if (t == "INSERT") expect(s.blocks, head_value(r, 2), place + " block");
        if (t == "DIMENSION") {
            const std::string block = head_value(r, 2);
            if (!block.empty()) expect(s.blocks, block, place + " block");
        }
    }
}

// ---------------------------------------------------------------------------
// The structure of an R13 or R14 file (plan phase 7, 7.22; format profile doc/reference/hdxf_r13_r14_format.md):
// sections, header in the target version's order, tables and their counts (D5), record forms, BLOCK/ENDBLK,
// children of POLYLINE and INSERT, the older ownership (family N: the reactor group; family A2000: 330),
// CLASSES (D4), DIMSTYLE (section 8) and the line between a helper graph a file brought along (kept as read)
// and native 2000 data (never made). No golden files: every rule here is a rule of the references or an
// invariant of a file with itself.
// ---------------------------------------------------------------------------

// R13's header in the order of AutoCAD's own R13 files (profile 3.2: 162 names), R14's after it
inline const std::vector<std::string>& older_header_names(hdxf::DxfVersion version) {
    static const std::vector<std::string> r13 = [] {
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
    }();
    static const std::vector<std::string> r14 = [] {
        std::vector<std::string> out;
        for (const auto& n : r13) {
            if (n == "$SAVEIMAGES") continue;
            out.push_back(n);
            if (n == "$ACADVER") out.push_back("$ACADMAINTVER");
        }
        out.push_back("$PROXYGRAPHICS");
        out.push_back("$MEASUREMENT");
        return out;
    }();
    return version == hdxf::DxfVersion::R13 ? r13 : r14;
}

// The header's variables as (name, groups)
inline std::vector<std::pair<std::string, std::vector<Group>>> header_variables(const std::vector<Rec>& recs) {
    std::vector<std::pair<std::string, std::vector<Group>>> out;
    for (const auto& r : recs) {
        if (r.type() != "SECTION" || r.value(2) != "HEADER") continue;
        for (size_t i = 2; i < r.groups.size(); ++i) {
            if (r.groups[i].first == 9) out.push_back({r.groups[i].second, {}});
            else if (!out.empty()) out.back().second.push_back(r.groups[i]);
        }
    }
    return out;
}

// What an R13 or R14 file is checked for (an R13/R14 file HDxf wrote)
struct OlderCheck {
    hdxf::DxfVersion version = hdxf::DxfVersion::R14;
    bool owner_groups = false;      // family A2000: 330 owner groups outside the reactor group (a drawing read from such a file)
    bool new_drawing = true;        // built in memory: every form is strict; false: a file read (kept as it was, plus what was added)
    const std::vector<Rec>* original = nullptr;     // the records of the file that was read: the helper graph it brought along
    std::set<std::string> extra_object_types;       // a program's own untyped objects (UnknownObjectData) besides the typed ones
};

inline std::string upper_text(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

inline std::vector<std::pair<std::string, std::string>> dictionary_entry_list(const Rec& dict) {
    std::vector<std::pair<std::string, std::string>> out;
    std::string key;
    bool in_list = false;
    for (const auto& [code, value] : dict.groups) {
        if (code == 102) { in_list = !value.empty() && value[0] == '{'; continue; }
        if (in_list) continue;
        if (code == 3) key = value;
        else if (code == 350 || code == 360) out.push_back({key, value});
    }
    return out;
}

inline std::vector<std::string> reactor_list_of(const Rec& r) {
    std::vector<std::string> out;
    bool in_reactors = false;
    for (const auto& [code, value] : r.groups) {
        if (code == 102) { in_reactors = value == "{ACAD_REACTORS"; continue; }
        if (code >= 1000) break;
        if (in_reactors && code == 330) out.push_back(value);
    }
    return out;
}

// The objects whose reactor is a dictionary that does not hold them: "TYPE <handle> <reactor>". A file of another
// program can have such a record; the writer keeps it as it was and does not make new ones
inline std::set<std::string> unheld_reactors(const std::vector<Rec>& recs) {
    std::map<std::string, const Rec*> objects;
    for (const auto& r : recs) {
        if (r.section == "OBJECTS" && r.type() != "SECTION" && r.type() != "ENDSEC" && !r.value(5).empty()) objects[upper_text(r.value(5))] = &r;
    }
    std::set<std::string> out;
    for (const auto& [handle, r] : objects) {
        for (const std::string& reactor : reactor_list_of(*r)) {
            auto owner = objects.find(upper_text(reactor));
            if (owner == objects.end() || owner->second->type() != "DICTIONARY") continue;
            bool holds = false;
            for (const auto& e : dictionary_entry_list(*owner->second)) holds = holds || upper_text(e.second) == handle;
            if (!holds) out.insert(r->type() + " " + r->value(5) + " " + reactor);
        }
    }
    return out;
}

inline void check_older_structure(const std::vector<Rec>& recs, const OlderCheck& o, const std::string& what) {
    using hdxf::DxfVersion;
    using hdxf_test::assert_eq;
    using hdxf_test::assert_false;
    using hdxf_test::assert_true;
    const bool r13 = o.version == DxfVersion::R13;
    check_older_records(recs, o.owner_groups, what, o.original);
    check_references(recs, what, o.new_drawing);

    std::map<uint64_t, const Rec*> by_handle;
    for (const auto& r : recs) {
        const std::string& t = r.type();
        if (r.section.empty() || r.section == "HEADER" || r.section == "CLASSES" || t == "SECTION" || t == "ENDSEC" || t == "ENDTAB") continue;
        std::string h = r.value(t == "DIMSTYLE" ? 105 : 5);
        if (!h.empty()) by_handle[handle_value(h)] = &r;
    }
    auto find_record = [&](const std::string& h) -> const Rec* {
        if (h.empty() || h == "0") return nullptr;
        auto it = by_handle.find(handle_value(h));
        return it == by_handle.end() ? nullptr : it->second;
    };

    // --- Sections: HEADER, CLASSES, TABLES, BLOCKS, ENTITIES, OBJECTS; none of 2004's or 2013's ---
    {
        const std::vector<std::string> order = {"HEADER", "CLASSES", "TABLES", "BLOCKS", "ENTITIES", "OBJECTS"};
        std::vector<std::string> sections;
        for (const auto& r : recs) if (r.type() == "SECTION") sections.push_back(r.value(2));
        if (o.new_drawing) {
            assert_true(sections == order, what + ": the six sections in order");
        } else {
            size_t at = 0;
            for (const auto& s : sections) {
                size_t i = at;
                while (i < order.size() && order[i] != s) ++i;
                assert_true(i < order.size(), what + ": section " + s + " out of place or not an R13/R14 section");
                at = i + 1;
            }
        }
    }

    // --- Header: the target version's variables in its order, none of 2000's ---
    {
        auto vars = header_variables(recs);
        assert_false(vars.empty(), what + ": a header");
        std::vector<std::string> names;
        std::string acadver, maint;
        for (const auto& [name, groups] : vars) {
            names.push_back(name);
            if (name == "$ACADVER" && !groups.empty()) acadver = groups[0].second;
            if (name == "$ACADMAINTVER" && !groups.empty()) maint = groups[0].second;
        }
        assert_eq(acadver, std::string(hdxf::acadver_string(o.version)), what + ": $ACADVER");
        static const std::set<std::string> later = {"$LASTSAVEDBY", "$REQUIREDVERSIONS", "$XCLIPFRAME", "$CELWEIGHT", "$ENDCAPS",
                                                    "$JOINSTYLE", "$LWDISPLAY", "$INSUNITS", "$EXTNAMES", "$PSTYLEMODE", "$FINGERPRINTGUID",
                                                    "$VERSIONGUID", "$TSTACKALIGN", "$TSTACKSIZE", "$CEPSNTYPE", "$HYPERLINKBASE",
                                                    "$STYLESHEET", "$XEDIT", "$CMATERIAL", "$SORTENTS", "$INDEXCTL", "$OLESTARTUP",
                                                    "$DIMADEC", "$DIMDSEP", "$DIMLUNIT", "$DIMFRAC", "$DIMATFIT", "$DIMTMOVE",
                                                    "$DIMLDRBLK", "$DIMLWD", "$DIMLWE", "$DIMLTYPE", "$DIMTXTDIRECTION",
                                                    "$UCSBASE", "$UCSORTHOREF", "$HALOGAP", "$OBSCOLOR", "$INTERSECTIONDISPLAY",
                                                    "$PROJECTNAME"};
        for (const auto& n : names) assert_false(later.count(n) > 0, what + ": " + n + " in an R13/R14 header");
        if (r13) assert_true(maint.empty() && std::count(names.begin(), names.end(), "$ACADMAINTVER") == 0, what + ": R13 has no $ACADMAINTVER");
        const auto& target = older_header_names(o.version);
        if (o.new_drawing) {
            assert_true(names == target, what + ": the header variables in " + (r13 ? "R13" : "R14") + "'s order (" + std::to_string(names.size()) + " of " + std::to_string(target.size()) + ")");
            assert_eq(maint, r13 ? std::string() : std::string("0"), what + ": $ACADMAINTVER of a new drawing");
        } else {
            // a file read keeps its header: the variables the target has are in the target's order
            long last = -1;
            for (const auto& n : names) {
                auto it = std::find(target.begin(), target.end(), n);
                if (it == target.end()) continue;
                long at = it - target.begin();
                assert_true(at > last, what + ": " + n + " out of the order of the version's header");
                last = at;
            }
        }
    }

    // --- Tables: order, counts (D5), the forms of a new drawing's records ---
    {
        const std::vector<std::string> nine = {"VPORT", "LTYPE", "LAYER", "STYLE", "VIEW", "UCS", "APPID", "DIMSTYLE", "BLOCK_RECORD"};
        std::vector<std::string> order;
        std::map<std::string, int> declared, actual, original_declared;
        std::string current;
        if (o.original) {
            for (const auto& r : *o.original) {
                if (r.section == "TABLES" && r.type() == "TABLE") original_declared[r.value(2)] = std::atoi(r.value(70).c_str());
            }
        }
        for (const auto& r : recs) {
            if (r.section != "TABLES") continue;
            const std::string& t = r.type();
            if (t == "SECTION" || t == "ENDSEC") continue;
            if (t == "TABLE") {
                current = r.value(2);
                order.push_back(current);
                declared[current] = std::atoi(r.value(70).c_str());
                assert_false(r.value(330).size() > 0 && !o.owner_groups, what + ": TABLE " + current + " has an owner");
                if (o.new_drawing) {
                    std::vector<std::string> markers;
                    for (const auto& g : r.groups) if (g.first == 100) markers.push_back(g.second);
                    assert_true(markers == std::vector<std::string>{"AcDbSymbolTable"}, what + ": TABLE " + current + " has only AcDbSymbolTable");
                }
            } else if (t == "ENDTAB") {
                current.clear();
            } else {
                ++actual[current];
            }
        }
        if (o.new_drawing) assert_true(order == nine, what + ": the nine tables in AutoCAD's order");
        for (const auto& name : order) assert_true(std::find(nine.begin(), nine.end(), name) != nine.end(), what + ": table " + name);
        auto position = [&](const char* n) { return std::find(order.begin(), order.end(), n) - order.begin(); };
        if (position("LTYPE") < static_cast<long>(order.size()) && position("LAYER") < static_cast<long>(order.size())) {
            assert_true(position("LTYPE") < position("LAYER"), what + ": LTYPE before LAYER");
        }
        for (const auto& name : order) {
            const int n = actual[name];
            const bool not_counted = name == "LTYPE" || name == "BLOCK_RECORD";      // BYBLOCK, BYLAYER / the two spaces
            const int records = not_counted ? std::max(n - 2, 0) : n;
            const std::string where = what + ": " + name + " 70 (" + std::to_string(declared[name]) + ") of " + std::to_string(n) + " records";
            if (o.new_drawing && name == "VPORT" && n == 1) assert_eq(declared[name], r13 ? 2 : 1, where);
            else if (o.new_drawing && name != "VPORT") assert_eq(declared[name], records, where);
            else if (original_declared.count(name) > 0) {
                // a table of a file read keeps the file's count (AutoCAD does not lower it); that the writer raises it
                // for every record it adds is 7.18's (the records it makes itself: layer 0, DEFPOINTS)
                assert_true(declared[name] >= original_declared[name], where + ", the file's was " + std::to_string(original_declared[name]));
            } else {
                assert_true(declared[name] >= records, where);
            }
        }
    }
    if (o.new_drawing) {
        static const std::map<std::string, std::set<int>> allowed = {
            {"VPORT", {0, 5, 100, 2, 70, 10, 20, 11, 21, 12, 22, 13, 23, 14, 24, 15, 25, 16, 26, 36, 17, 27, 37, 40, 41, 42, 43, 44, 50, 51, 71, 72, 73, 74, 75, 76, 77, 78}},
            {"LTYPE", {0, 5, 100, 2, 70, 3, 72, 73, 40, 49, 74, 75, 340, 46, 50, 44, 45, 9}},
            {"LAYER", {0, 5, 100, 2, 70, 62, 6}},
            {"STYLE", {0, 5, 100, 2, 70, 40, 41, 50, 71, 42, 3, 4}},
            {"VIEW", {0, 5, 100, 2, 70, 40, 10, 20, 41, 11, 21, 31, 12, 22, 32, 42, 43, 44, 50, 71}},
            {"UCS", {0, 5, 100, 2, 70, 10, 20, 30, 11, 21, 31, 12, 22, 32}},
            {"APPID", {0, 5, 100, 2, 70}},
            {"DIMSTYLE", {0, 105, 100, 2, 70, 3, 4, 5, 6, 7, 40, 41, 42, 43, 44, 45, 46, 47, 48, 140, 141, 142, 143, 144, 145, 146, 147,
                          71, 72, 73, 74, 75, 76, 77, 78, 170, 171, 172, 173, 174, 175, 176, 177, 178, 270, 271, 272, 273, 274, 340,
                          275, 280, 281, 282, 283, 284, 285, 286, 287, 288}},
            {"BLOCK_RECORD", {0, 5, 100, 2}},
        };
        for (const auto& r : recs) {
            if (r.section != "TABLES" || allowed.count(r.type()) == 0) continue;
            const auto& codes = allowed.at(r.type());
            bool xdata = false;
            bool in_list = false;
            std::vector<std::string> markers;
            for (const auto& [code, value] : r.groups) {
                if (code == 102) { in_list = !value.empty() && value[0] == '{'; continue; }
                if (code >= 1000) xdata = true;
                if (xdata || in_list) continue;
                if (code == 100) markers.push_back(value);
                if (code == 330 || code == 360) continue;           // owner (family A2000) and extension dictionary
                assert_true(codes.count(code) > 0, what + ": " + std::to_string(code) + " in a " + r.type() + " " + r.value(2));
            }
            const std::string subclass = r.type() == "VPORT" ? "AcDbViewportTableRecord" : r.type() == "LTYPE" ? "AcDbLinetypeTableRecord"
                : r.type() == "LAYER" ? "AcDbLayerTableRecord" : r.type() == "STYLE" ? "AcDbTextStyleTableRecord"
                : r.type() == "VIEW" ? "AcDbViewTableRecord" : r.type() == "UCS" ? "AcDbUCSTableRecord"
                : r.type() == "APPID" ? "AcDbRegAppTableRecord" : r.type() == "DIMSTYLE" ? "AcDbDimStyleTableRecord" : "AcDbBlockTableRecord";
            assert_true(markers == std::vector<std::string>({"AcDbSymbolTableRecord", subclass}),
                        what + ": the subclass markers of " + r.type() + " " + r.value(2));
        }
    }

    // --- DIMSTYLE (section 8): the style record's handle in 340, the arrows named, all groups in a new drawing ---
    {
        std::set<std::string> block_names;
        for (const auto& r : recs) if (r.section == "TABLES" && r.type() == "BLOCK_RECORD") block_names.insert(upper_text(r.value(2)));
        for (const auto& r : recs) {
            if (r.section != "TABLES" || r.type() != "DIMSTYLE") continue;
            const std::string place = what + ": DIMSTYLE " + r.value(2);
            assert_true(!r.value(105).empty(), place + " has its handle in 105 (5 is the name of DIMBLK)");
            const std::string style = r.value(340);
            if (!style.empty()) {
                const Rec* s = find_record(style);
                assert_true(s != nullptr && s->type() == "STYLE", place + ": 340 is the handle of a STYLE record");
            }
            if (!o.new_drawing) continue;
            for (int code : {3, 4, 5, 6, 7, 40, 41, 42, 43, 44, 45, 46, 47, 48, 140, 141, 142, 143, 144, 145, 146, 147, 71, 72, 73, 74, 75, 76,
                             77, 78, 170, 171, 172, 173, 174, 175, 176, 177, 178, 270, 271, 272, 273, 274, 340, 275, 280, 281, 282, 283, 284,
                             285, 286, 287, 288}) {
                bool present = false;
                for (const auto& g : r.groups) present = present || g.first == code;
                assert_true(present, place + ": R13/R14 write every group, " + std::to_string(code) + " is missing");
            }
            for (int code : {5, 6, 7}) {
                const std::string arrow = r.value(code);
                if (arrow.empty()) continue;
                const std::string key = upper_text(arrow);
                const bool known = block_names.count(key) > 0 || hdxf::internal::is_builtin_arrow(arrow) ||
                                   hdxf::internal::is_builtin_arrow("_" + arrow) || key == "_CLOSEDFILLED" || key == "CLOSEDFILLED";
                assert_true(known, place + ": the arrow " + std::to_string(code) + " \"" + arrow + "\" is no block of the file and no built-in arrow");
            }
        }
    }

    // --- BLOCKS: BLOCK_RECORD <-> BLOCK, the space blocks first, ENDBLK after each ---
    {
        std::vector<std::string> blocks;
        std::set<std::string> records;
        for (const auto& r : recs) {
            if (r.section == "TABLES" && r.type() == "BLOCK_RECORD") records.insert(upper_text(r.value(2)));
            if (r.section == "BLOCKS" && r.type() == "BLOCK") blocks.push_back(r.value(2));
        }
        int begin = 0, end = 0;
        for (const auto& r : recs) {
            if (r.section != "BLOCKS") continue;
            begin += r.type() == "BLOCK";
            end += r.type() == "ENDBLK";
            assert_true(end <= begin && begin - end <= 1, what + ": BLOCK and ENDBLK alternate");
        }
        assert_eq(begin, end, what + ": every BLOCK has its ENDBLK");
        if (!records.empty()) {
            for (const auto& b : blocks) assert_true(records.count(upper_text(b)) > 0, what + ": block " + b + " has no BLOCK_RECORD");
            std::set<std::string> in_blocks;
            for (const auto& b : blocks) in_blocks.insert(upper_text(b));
            for (const auto& b : records) assert_true(in_blocks.count(b) > 0, what + ": BLOCK_RECORD " + b + " has no block");
        }
        if (o.new_drawing) {
            assert_true(blocks.size() >= 2 && blocks[0] == "*MODEL_SPACE" && blocks[1] == "*PAPER_SPACE", what + ": *MODEL_SPACE and *PAPER_SPACE first");
            for (const auto& r : recs) {
                if (r.section != "BLOCKS") continue;
                if (r.type() == "BLOCK") {
                    std::vector<std::string> markers;
                    for (const auto& g : r.groups) if (g.first == 100) markers.push_back(g.second);
                    assert_true(markers == std::vector<std::string>({"AcDbEntity", "AcDbBlockBegin"}), what + ": the subclasses of BLOCK " + r.value(2));
                    assert_eq(value_after_subclass(r, "AcDbBlockBegin", 3), r.value(2), what + ": BLOCK " + r.value(2) + " repeats its name in 3");
                    assert_eq(r.value(67), r.value(2) == "*PAPER_SPACE" ? "1" : "", what + ": 67 of BLOCK " + r.value(2));
                } else if (r.type() == "ENDBLK") {
                    std::vector<std::string> markers;
                    for (const auto& g : r.groups) if (g.first == 100) markers.push_back(g.second);
                    assert_true(markers == std::vector<std::string>({"AcDbEntity", "AcDbBlockEnd"}), what + ": the subclasses of ENDBLK");
                }
            }
        }
    }

    // --- ENTITIES and the entities of blocks: children, heads, the paper space order of R13 ---
    {
        for (const char* section : {"BLOCKS", "ENTITIES"}) {
            std::string parent;             // POLYLINE or INSERT while its children follow
            std::string parent_space;
            for (const auto& r : recs) {
                if (r.section != section) continue;
                const std::string& t = r.type();
                if (t == "SECTION" || t == "ENDSEC") continue;
                if (t == "BLOCK") { parent.clear(); continue; }
                if (t == "ENDBLK") {
                    assert_true(parent.empty(), what + ": a " + parent + " without SEQEND at the end of a block");
                    continue;
                }
                if (!parent.empty()) {
                    const bool child = (parent == "POLYLINE" && t == "VERTEX") || (parent == "INSERT" && t == "ATTRIB");
                    if (t == "SEQEND") {
                        if (o.new_drawing) assert_eq(r.value(67), parent_space, what + ": the SEQEND of a " + parent + " is in its space");
                        parent.clear();
                    } else {
                        assert_true(child, what + ": " + t + " inside a " + parent);
                        if (o.new_drawing) assert_eq(r.value(67), parent_space, what + ": the " + t + " of a " + parent + " is in its space");
                    }
                    continue;
                }
                assert_true(t != "VERTEX" && t != "SEQEND" && t != "ATTRIB", what + ": " + t + " without a POLYLINE or INSERT");
                if (t == "POLYLINE" || (t == "INSERT" && r.value(66) == "1")) {
                    parent = t;
                    parent_space = r.value(67);
                }
                if (o.new_drawing) {
                    std::string first_subclass;
                    for (const auto& g : r.groups) if (g.first == 100) { first_subclass = g.second; break; }
                    assert_eq(first_subclass, std::string("AcDbEntity"), what + ": the first subclass of " + t);
                    for (const auto& g : r.groups) {
                        if (g.first >= 1000) break;
                        assert_true(g.first != 347 && g.first != 284, what + ": " + std::to_string(g.first) + " in a " + t);
                    }
                }
            }
            assert_true(parent.empty(), what + ": a " + parent + " without SEQEND at the end of " + section);
        }
        if (o.new_drawing && r13) {
            // R13: all the paper space entities, then the model space ones (R13:BLOCKS; D6)
            bool model_seen = false;
            for (const auto& r : recs) {
                if (r.section != "ENTITIES" || r.type() == "SECTION" || r.type() == "ENDSEC") continue;
                const bool paper = r.value(67) == "1";
                assert_false(paper && model_seen, what + ": R13 writes paper space before model space (" + r.type() + ")");
                model_seen = model_seen || !paper;
            }
        }
    }

    // --- OBJECTS: the root first, the older ownership, the helper graph ---
    {
        std::vector<const Rec*> objects;
        for (const auto& r : recs) if (r.section == "OBJECTS" && r.type() != "SECTION" && r.type() != "ENDSEC") objects.push_back(&r);
        std::map<std::string, int> in_original;
        std::set<std::string> original_root_names;
        if (o.original) {
            bool first = true;
            for (const auto& r : *o.original) {
                if (r.section != "OBJECTS" || r.type() == "SECTION" || r.type() == "ENDSEC") continue;
                ++in_original[r.type()];
                if (first && r.type() == "DICTIONARY") for (const auto& e : dictionary_entry_list(r)) original_root_names.insert(e.first);
                first = false;
            }
        }
        if (!objects.empty()) {
            const Rec& root = *objects[0];
            assert_eq(root.type(), std::string("DICTIONARY"), what + ": the root dictionary is the first object");
            if (o.new_drawing) {
                for (const auto& g : root.groups) assert_true(g.first != 102 && g.first != 330 && g.first < 1000, what + ": the root dictionary has no reactors or owner");
                std::set<std::string> names;
                for (const auto& e : dictionary_entry_list(root)) names.insert(e.first);
                assert_true(names.count("ACAD_GROUP") > 0 && names.count("ACAD_MLINESTYLE") > 0, what + ": the root has ACAD_GROUP and ACAD_MLINESTYLE");
                for (const auto& n : names) {
                    assert_true(n == "ACAD_GROUP" || n == "ACAD_MLINESTYLE" || n == "ACDBVARIABLEDICTIONARY" || n == "ACAD_IMAGE_DICT" || n == "ACAD_IMAGE_VARS",
                                what + ": the root dictionary of a new drawing has no " + n + " (native 2000 data)");
                }
            } else if (o.original) {
                for (const auto& e : dictionary_entry_list(root)) {
                    const std::string& n = e.first;
                    const bool made = n == "ACAD_GROUP" || n == "ACAD_MLINESTYLE" || n == "ACDBVARIABLEDICTIONARY" || n == "ACAD_IMAGE_DICT" || n == "ACAD_IMAGE_VARS";
                    assert_true(made || original_root_names.count(n) > 0, what + ": the root dictionary got " + n + ", which the file did not have");
                }
            }
        }
        // the types: a new drawing has the typed ones and the program's own; a read file those it had besides
        static const std::set<std::string> base_types = {"DICTIONARY", "GROUP", "MLINESTYLE", "XRECORD", "DICTIONARYVAR", "IMAGEDEF",
                                                         "IMAGEDEF_REACTOR", "RASTERVARIABLES"};
        for (const auto* r : objects) {
            const std::string& t = r->type();
            const bool known = base_types.count(t) > 0 || o.extra_object_types.count(t) > 0;
            if (o.new_drawing) assert_true(known, what + ": a " + t + " in a new drawing's OBJECTS (native or untyped data nobody made)");
            else if (!known) assert_true(in_original[t] > 0, what + ": a " + t + " that the file read did not have");
            assert_false(t == "LAYOUT" || t == "PLOTSETTINGS" || t == "ACDBPLACEHOLDER" || t == "ACDBDICTIONARYWDFLT" ? o.new_drawing : false,
                         what + ": " + t + " in a new drawing");
        }
        // the older ownership: a record's reactor that is a dictionary holds the record; the typed objects are held by a dictionary
        std::set<std::string> held;
        for (const auto* r : objects) {
            if (r->type() != "DICTIONARY") continue;
            for (const auto& e : dictionary_entry_list(*r)) held.insert(e.second);
        }
        const std::set<std::string> original_unheld = o.original ? unheld_reactors(*o.original) : std::set<std::string>();
        for (size_t i = 0; i < objects.size(); ++i) {
            const Rec& r = *objects[i];
            const std::string self = r.value(5);
            for (const std::string& reactor : reactor_list_of(r)) {
                const Rec* owner = find_record(reactor);
                if (!owner || owner->type() != "DICTIONARY") continue;
                if (original_unheld.count(r.type() + " " + self + " " + reactor) > 0) continue;
                bool holds = false;
                for (const auto& e : dictionary_entry_list(*owner)) holds = holds || (e.second == self);
                assert_true(holds, what + ": " + r.type() + " " + self + " has the reactor " + reactor + ", a dictionary that does not hold it");
            }
            if (o.owner_groups) {
                // family A2000: the 330 before the subclasses is the owner
                std::string owner;
                bool in_list = false;
                for (const auto& [code, value] : r.groups) {
                    if (code == 100) break;
                    if (code == 102) { in_list = !value.empty() && value[0] == '{'; continue; }
                    if (code == 330 && !in_list) { owner = value; break; }
                }
                if (!owner.empty() && owner != "0") assert_true(find_record(owner) != nullptr, what + ": the owner " + owner + " of " + r.type() + " " + self);
            }
            const std::string& t = r.type();
            if (o.new_drawing && i > 0 && (t == "DICTIONARY" || t == "GROUP" || t == "MLINESTYLE" || t == "IMAGEDEF" || t == "RASTERVARIABLES")) {
                assert_true(held.count(self) > 0, what + ": " + t + " " + self + " is held by no dictionary");
            }
            if (t == "GROUP") {
                for (const auto& [code, value] : r.groups) {
                    if (code != 340) continue;
                    const Rec* member = find_record(value);
                    assert_true(member && (member->section == "ENTITIES" || member->section == "BLOCKS"), what + ": GROUP member " + value + " is an entity");
                }
            }
        }
    }

    // --- CLASSES (D4): a record per class the objects need in a new drawing; the whole register of a file read ---
    {
        struct ClassForm { const char* cpp; int flags; int instance; };
        static const std::map<std::string, ClassForm> table = {
            {"LWPOLYLINE", {"AcDbPolyline", 0, 1}}, {"IMAGE", {"AcDbRasterImage", 127, 1}}, {"OLE2FRAME", {"AcDbOle2Frame", 0, 1}},
            {"HATCH", {"AcDbHatch", 0, 1}}, {"DICTIONARYVAR", {"AcDbDictionaryVar", 0, 0}}, {"RASTERVARIABLES", {"AcDbRasterVariables", 0, 0}},
            {"IMAGEDEF", {"AcDbRasterImageDef", 0, 0}}, {"IMAGEDEF_REACTOR", {"AcDbRasterImageDefReactor", 1, 0}},
            {"IDBUFFER", {"AcDbIdBuffer", 0, 0}}, {"SPATIAL_FILTER", {"AcDbSpatialFilter", 0, 0}}, {"XRECORD", {"AcDbXrecord", 0, 0}},
            {"SORTENTSTABLE", {"AcDbSortentsTable", 0, 0}}, {"LAYER_INDEX", {"AcDbLayerIndex", 0, 0}}, {"SPATIAL_INDEX", {"AcDbSpatialIndex", 0, 0}},
            {"OBJECT_PTR", {"CAseDLPNTableRecord", 1, 0}}};
        std::set<std::string> classes, needed;
        int class_count = 0;
        for (const auto& r : recs) {
            if (r.section == "CLASSES" && r.type() == "CLASS") {
                ++class_count;
                classes.insert(r.value(1));
                if (o.new_drawing) {
                    std::vector<int> codes;
                    for (const auto& g : r.groups) codes.push_back(g.first);
                    assert_true(codes == std::vector<int>({0, 1, 2, 3, 90, 280, 281}), what + ": the groups of CLASS " + r.value(1));
                    auto it = table.find(r.value(1));
                    assert_true(it != table.end(), what + ": CLASS " + r.value(1) + " is none of R14's");
                    if (it != table.end()) {
                        assert_eq(r.value(2), std::string(it->second.cpp), what + ": the C++ class of " + r.value(1));
                        assert_eq(std::atoi(r.value(90).c_str()), r13 ? 32768 : it->second.flags, what + ": the proxy flags of " + r.value(1));
                        assert_eq(std::atoi(r.value(281).c_str()), it->second.instance, what + ": 281 of " + r.value(1));
                        assert_eq(std::atoi(r.value(280).c_str()), 0, what + ": 280 of " + r.value(1));
                        assert_false(r.value(3).empty(), what + ": the application of " + r.value(1));
                    }
                }
            }
            if (r.section == "ENTITIES" || r.section == "BLOCKS") {
                const std::string& t = r.type();
                if (t == "LWPOLYLINE" || t == "HATCH" || t == "IMAGE" || t == "OLE2FRAME") needed.insert(t);
            }
            if (r.section == "OBJECTS" && r.type() != "SECTION" && r.type() != "ENDSEC") {
                const std::string& t = r.type();
                if (t != "DICTIONARY" && t != "GROUP" && t != "MLINESTYLE") needed.insert(t);
            }
        }
        // the proxy objects' class index: 500 and up, in the order of the register
        for (const auto& r : recs) {
            if (r.type() != "ACAD_PROXY_ENTITY" && r.type() != "ACAD_PROXY_OBJECT") continue;
            const std::string id = value_after_subclass(r, r.type() == "ACAD_PROXY_ENTITY" ? "AcDbProxyEntity" : "AcDbProxyObject", 91);
            if (id.empty()) continue;
            const int n = std::atoi(id.c_str());
            assert_true(n >= 500 && n < 500 + class_count, what + ": the class index " + id + " of a " + r.type() + " is not in the register of " + std::to_string(class_count));
        }
        if (o.new_drawing) assert_true(classes == needed, what + ": CLASSES hold the " + std::to_string(needed.size()) + " classes the drawing uses, not " + std::to_string(classes.size()));
    }
}

// The same for a file's bytes (ASCII or binary)
inline void check_older_file(const std::vector<uint8_t>& data, const OlderCheck& o, const std::string& what) {
    check_older_structure(records_of_file(data), o, what);
}

// Two files of the same drawing, one ASCII and one binary, are the same records with the same groups (numbers by value)
inline void check_same_groups(const std::vector<Rec>& a, const std::vector<Rec>& b, const std::string& what) {
    using hdxf::internal::GroupValueType;
    hdxf_test::assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()), what + ": record count");
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        hdxf_test::assert_eq(static_cast<int>(a[i].groups.size()), static_cast<int>(b[i].groups.size()), what + ": groups of record " + std::to_string(i) + " " + a[i].type());
        for (size_t k = 0; k < a[i].groups.size() && k < b[i].groups.size(); ++k) {
            const auto& x = a[i].groups[k];
            const auto& y = b[i].groups[k];
            const std::string where = what + ": record " + std::to_string(i) + " " + a[i].type() + " group " + std::to_string(k);
            hdxf_test::assert_eq(x.first, y.first, where + " code");
            if (hdxf::internal::group_value_type(x.first) == GroupValueType::Double) {
                const double u = std::atof(x.second.c_str());
                const double v = std::atof(y.second.c_str());
                hdxf_test::assert_true(std::fabs(u - v) <= 1e-12 * std::max(1.0, std::fabs(u)), where + ": " + x.second + " / " + y.second);
            } else {
                hdxf_test::assert_eq(x.second, y.second, where);
            }
        }
    }
}

} // namespace dxf_structure
