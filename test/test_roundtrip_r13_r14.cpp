#include "test_framework.hpp"
#include "dxf_structure.hpp"
#include "hdxf.hpp"
#include "text_encoding.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// R13/R14 files read and written again (plan phase 7, 7.18 and 7.19, B12, B96, B97). No golden files:
// the facit of an unchanged file is the file itself, of a change what the drawing says now against
// what the file said — every other record, and every other group of the changed record, stays.
//
// The files are the R13/R14 files of build/test-data (ezdxf, LibreDWG, gvSIG, SpatialAuxetic, jscad) and
// the local ones in build/test-data/local-r13r14 (PROVENANCE.txt); a test skips what is missing.
// ---------------------------------------------------------------------------

namespace {

namespace fs = std::filesystem;
using dxf_structure::Rec;
using hdxf::DxfFormat;
using hdxf::internal::GroupValueType;
using hdxf::internal::group_value_type;

fs::path data_dir() { return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR); }

std::vector<uint8_t> read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool is_binary(const std::vector<uint8_t>& bytes) {
    static const char sentinel[] = "AutoCAD Binary DXF";
    return bytes.size() > 18 && std::equal(sentinel, sentinel + 18, bytes.begin());
}

std::string name_of(DxfFormat f) { return f == DxfFormat::Ascii ? "ASCII" : "binary"; }
DxfFormat other(DxfFormat f) { return f == DxfFormat::Ascii ? DxfFormat::Binary : DxfFormat::Ascii; }

struct Source {
    std::string label;
    fs::path path;
    std::vector<uint8_t> bytes;
    DxfFormat format = DxfFormat::Ascii;
};

// The R13/R14 files there are: the pinned ones and every file of the local directory
std::vector<Source> older_files() {
    std::vector<fs::path> paths;
    const char* pinned[] = {"ezdxf/small_r13.dxf",   "ezdxf/bin_dxf_r13.dxf", "ezdxf/small_r14.dxf",
                            "ezdxf/bin_dxf_r14.dxf", "libre/r13_v.dxf",       "gvsig/dxfprueba.dxf",
                            "spatialauxetic/HEAD.DXF", "spatialauxetic/simp.dxf", "jscad/pyramid-base.dxf"};
    for (const char* p : pinned) paths.push_back(data_dir() / p);
    std::error_code ec;
    std::vector<fs::path> local;
    for (const auto& entry : fs::directory_iterator(data_dir() / "local-r13r14", ec)) {
        std::string ext = entry.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (entry.is_regular_file() && ext == ".dxf") local.push_back(entry.path());
    }
    std::sort(local.begin(), local.end());
    paths.insert(paths.end(), local.begin(), local.end());

    std::vector<Source> out;
    for (const auto& p : paths) {
        if (!fs::exists(p, ec)) continue;
        Source s;
        s.label = p.filename().string();
        s.path = p;
        s.bytes = read_bytes(p);
        s.format = is_binary(s.bytes) ? DxfFormat::Binary : DxfFormat::Ascii;
        out.push_back(std::move(s));
    }
    return out;
}

// The failures of a loop over files, reported together
struct Failures {
    std::vector<std::string> list;
    void add(const std::string& what) { list.push_back(what); }
    void check() const {
        std::string all;
        for (const auto& f : list) all += "\n  " + f;
        hdxf_test::assert_true(list.empty(), std::to_string(list.size()) + " failure(s):" + all);
    }
};

// A file ends in an open section (spatialauxetic/HEAD.DXF stops after "2 ENTITIES": a template of a MATLAB
// toolbox, not a complete file). Written, it is completed (ENDSEC, the structure of OBJECTS, EOF); it
// is the one file of the set that is not written as it is
bool is_unterminated(const std::vector<uint8_t>& bytes) {
    std::string text(bytes.begin(), bytes.end());
    return text.find("EOF") == std::string::npos && !is_binary(bytes);
}

using Flat = std::vector<std::pair<int, std::string>>;

Flat flat_groups(const std::vector<uint8_t>& bytes) {
    Flat out;
    for (const auto& r : dxf_structure::records_of_file(bytes)) {
        for (const auto& g : r.groups) out.push_back(g);
    }
    return out;
}

// The groups of the same drawing in ASCII and binary: the same codes and values (numbers by value; the
// carets of ASCII undone). `a_ascii`, `b_ascii`: the group lists are of ASCII files. Empty when equal, else
// what differs first
std::string group_difference(const Flat& a, const Flat& b, bool a_ascii, bool b_ascii) {
    if (a.size() != b.size()) return "group count " + std::to_string(a.size()) + " / " + std::to_string(b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        const std::string where = "group " + std::to_string(i) + " (" + std::to_string(a[i].first) + ")";
        if (a[i].first != b[i].first) return where + ": code " + std::to_string(b[i].first);
        GroupValueType type = group_value_type(a[i].first);
        if (type == GroupValueType::Double) {
            double x = std::strtod(a[i].second.c_str(), nullptr);
            double y = std::strtod(b[i].second.c_str(), nullptr);
            if (!(x == y || std::fabs(x - y) <= 1e-13 * std::max(1.0, std::fabs(x)) || (std::isnan(x) && std::isnan(y)))) {
                return where + ": " + a[i].second + " / " + b[i].second;
            }
            continue;
        }
        bool text = type == GroupValueType::String || type == GroupValueType::Unknown;
        std::string x = a[i].second, y = b[i].second;
        if (text) {
            // ASCII has its carets (^J), binary the bytes
            if (a_ascii) x = hdxf::internal::decode_carets(x);
            if (b_ascii) y = hdxf::internal::decode_carets(y);
        }
        if (x != y) return where + ": \"" + a[i].second + "\" / \"" + b[i].second + "\"";
    }
    return {};
}

// The index of the first byte that differs, or -1
long first_difference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) if (a[i] != b[i]) return static_cast<long>(i);
    return a.size() == b.size() ? -1 : static_cast<long>(n);
}

} // namespace

// ---------------------------------------------------------------------------
// 7.18, B12: a file read and written again is the file, byte for byte, in its own format
// ---------------------------------------------------------------------------

TEST(roundtrip_r13_r14_unchanged_file_is_written_byte_for_byte) {
    std::vector<Source> files = older_files();
    if (files.empty()) hdxf_test::skip("no R13/R14 test file");
    Failures failures;
    int identical = 0;
    for (const Source& s : files) {
        const std::string what = s.label + " " + name_of(s.format);
        hdxf::Drawing d;
        if (!d.load(s.path)) {
            failures.add(what + ": load: " + d.last_error());
            continue;
        }
        std::vector<uint8_t> out = d.save_to_memory(s.format);
        if (out.empty()) {
            failures.add(what + ": save: " + d.last_error());
            continue;
        }
        if (is_unterminated(s.bytes)) {
            // Every group of the file as it was, except $HANDSEED (above the records the writer made for the
            // objects the file lacked), then the end of the section and the structure the file did not have
            Flat before = flat_groups(std::vector<uint8_t>(s.bytes.begin(), s.bytes.end()));
            Flat after = flat_groups(out);
            bool same = after.size() > before.size();
            for (size_t i = 0; same && i < before.size(); ++i) {
                bool seed = i > 0 && before[i - 1] == std::make_pair(9, std::string("$HANDSEED"));
                if (before[i] != after[i] && !seed) same = false;
            }
            if (!same) failures.add(what + ": the completed file differs from the file in the groups it had");
            else ++identical;
            continue;
        }
        long at = first_difference(s.bytes, out);
        if (at >= 0) {
            failures.add(what + ": differs from byte " + std::to_string(at) + " (" + std::to_string(s.bytes.size()) +
                         " bytes in, " + std::to_string(out.size()) + " out)");
            continue;
        }
        ++identical;
        // Written again: the same, and from what was written: the same
        std::vector<uint8_t> twice = d.save_to_memory(s.format);
        if (twice != out) failures.add(what + ": the second save differs from the first");
        hdxf::Drawing again;
        if (!again.load_from_memory(out.data(), out.size())) {
            failures.add(what + ": the written file does not load: " + again.last_error());
            continue;
        }
        if (again.save_to_memory(s.format) != out) failures.add(what + ": a read file written twice differs");
    }
    failures.check();
    hdxf_test::assert_true(identical == static_cast<int>(files.size()), "every file written as it is");
}

// An unchanged drawing is written without anything a 2000 writer would give it: the sections, tables and
// handles of the file, and nothing else
TEST(roundtrip_r13_r14_unchanged_file_has_nothing_added) {
    std::vector<Source> files = older_files();
    if (files.empty()) hdxf_test::skip("no R13/R14 test file");
    Failures failures;
    for (const Source& s : files) {
        if (is_unterminated(s.bytes)) continue;
        hdxf::Drawing d;
        if (!d.load(s.path)) {
            failures.add(s.label + ": load: " + d.last_error());
            continue;
        }
        std::vector<Rec> before = dxf_structure::records_of_file(s.bytes);
        std::vector<uint8_t> out = d.save_to_memory(s.format);
        std::vector<Rec> after = dxf_structure::records_of_file(out);
        auto count = [](const std::vector<Rec>& recs, const std::string& section, const std::string& type) {
            int n = 0;
            for (const auto& r : recs) if (r.section == section && r.type() == type) ++n;
            return n;
        };
        for (const char* type : {"LAYOUT", "PLOTSETTINGS", "ACDBPLACEHOLDER", "SCALE", "VISUALSTYLE", "MATERIAL"}) {
            if (count(before, "OBJECTS", type) != count(after, "OBJECTS", type)) {
                failures.add(s.label + ": " + type + " objects added or removed");
            }
        }
        if (count(before, "CLASSES", "CLASS") != count(after, "CLASSES", "CLASS")) failures.add(s.label + ": CLASS records added");
        for (const char* layout : {"*Paper_Space0", "*PAPER_SPACE0"}) {
            if (count(after, "BLOCKS", "BLOCK") != count(before, "BLOCKS", "BLOCK")) failures.add(s.label + ": blocks added (" + layout + ")");
        }
        if (before.size() != after.size()) failures.add(s.label + ": the number of records changed");
    }
    failures.check();
}

// ---------------------------------------------------------------------------
// 7.18, B96: another format of the same drawing is the same groups and relations, also what HDxf does not know
// ---------------------------------------------------------------------------

TEST(roundtrip_r13_r14_other_format_is_the_same_groups) {
    std::vector<Source> files = older_files();
    if (files.empty()) hdxf_test::skip("no R13/R14 test file");
    Failures failures;
    for (const Source& s : files) {
        const DxfFormat to = other(s.format);
        const std::string what = s.label + " " + name_of(s.format) + " -> " + name_of(to);
        hdxf::Drawing d;
        if (!d.load(s.path)) {
            failures.add(what + ": load: " + d.last_error());
            continue;
        }
        std::vector<uint8_t> converted = d.save_to_memory(to);
        if (converted.empty()) {
            failures.add(what + ": save: " + d.last_error());
            continue;
        }
        Flat original = flat_groups(s.bytes);
        Flat now = flat_groups(converted);
        std::string why = group_difference(original, now, s.format == DxfFormat::Ascii, to == DxfFormat::Ascii);
        if (is_unterminated(s.bytes)) {
            // The groups the file had are the first ones
            if (now.size() < original.size()) failures.add(what + ": shorter than the file");
            continue;
        }
        if (!why.empty()) {
            failures.add(what + ": " + why);
            continue;
        }

        // The drawing read back from it has the same records and relations
        hdxf::Drawing back;
        if (!back.load_from_memory(converted.data(), converted.size())) {
            failures.add(what + ": the converted file does not load: " + back.last_error());
            continue;
        }
        if (back.entities().size() != d.entities().size() || back.objects().size() != d.objects().size() ||
            back.blocks().size() != d.blocks().size() || back.layers().size() != d.layers().size()) {
            failures.add(what + ": the drawing read back has other records");
        }
        for (size_t i = 0; i < d.objects().size() && i < back.objects().size(); ++i) {
            const hdxf::Object& a = d.objects()[i];
            const hdxf::Object& b = back.objects()[i];
            if (a.handle != b.handle || a.owner != b.owner || a.reactors != b.reactors) {
                failures.add(what + ": object " + a.handle + " has other owner or reactors");
                break;
            }
        }
        // And back in the first format: the groups of the file again (written from a drawing read from
        // the other format, the same fixed point)
        std::vector<uint8_t> again = back.save_to_memory(to);
        if (again != converted) failures.add(what + ": the converted file written again differs");
        std::vector<uint8_t> home = back.save_to_memory(s.format);
        if (home.empty()) {
            failures.add(what + ": back to the first format: " + back.last_error());
            continue;
        }
        Flat home_groups = flat_groups(home);
        std::string home_why = group_difference(original, home_groups, s.format == DxfFormat::Ascii, s.format == DxfFormat::Ascii);
        if (!home_why.empty()) failures.add(what + ": back to " + name_of(s.format) + ": " + home_why);
    }
    failures.check();
}

// ---------------------------------------------------------------------------
// 7.19, B96/B97: changes, additions and removals in a file read. What the file said stays as it said it:
// every record the change does not touch is the same group for group, a changed record differs in the
// groups of the change (a list in the whole of its area), no link is left to a record that is gone and no
// link made that leads nowhere, $HANDSEED stays above the handles, and the links the file already had
// broken (or to something outside the file) are left as they were
// ---------------------------------------------------------------------------

namespace {

// The handle of a record: 5, DIMSTYLE's 105, before the first subclass marker
std::string handle_of(const Rec& r) {
    for (const auto& g : r.groups) {
        if (g.first == 100) break;
        if (g.first == 5 || g.first == 105) return g.second;
    }
    return {};
}

std::string upper_case(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// A group that points to another record (a handle; not a number that happens to read as one)
bool is_pointer_code(int code) {
    return code == 330 || code == 331 || code == 340 || code == 342 || code == 343 || code == 344 || code == 350 ||
           code == 360 || code == 390;
}

bool is_structure_record(const Rec& r);

std::string upper_case(std::string s);
std::string handle_of(const Rec& r);

// Whether the record with this handle points to one of `handles` (capitals)
bool points_to_any(const std::vector<Rec>& recs, const std::string& key, const std::set<std::string>& handles) {
    for (const auto& r : recs) {
        if (upper_case(handle_of(r)) != key) continue;
        for (const auto& [code, value] : r.groups) {
            if (is_pointer_code(code) && handles.count(upper_case(value))) return true;
        }
    }
    return false;
}

bool is_structure_record(const Rec& r) {
    const std::string& t = r.type();
    return t == "SECTION" || t == "ENDSEC" || t == "ENDTAB" || t == "EOF";
}

struct Keyed {
    std::string key;
    const Rec* record;
};

// The records of a file by key: the handle when it has one, else where it stands (section, type, name,
// occurrence); the header as one record
std::vector<Keyed> keyed_records(const std::vector<Rec>& recs) {
    std::vector<Keyed> out;
    std::map<std::string, int> seen;
    for (const auto& r : recs) {
        if (r.type() == "SECTION" && r.value(2) == "HEADER") {
            out.push_back({"HEADER", &r});
            continue;
        }
        if (is_structure_record(r)) continue;
        std::string h = handle_of(r);
        if (!h.empty()) {
            out.push_back({upper_case(h), &r});
            continue;
        }
        std::string where = r.section + ":" + r.type() + ":" + (r.type() == "CLASS" ? r.value(1) : r.value(2));
        out.push_back({where + "#" + std::to_string(seen[where]++), &r});
    }
    return out;
}

// The header variables of the header record, name to its groups
std::map<std::string, std::string> header_variables(const Rec& header) {
    std::map<std::string, std::string> out;
    std::string name;
    for (const auto& [code, value] : header.groups) {
        if (code == 9) {
            name = value;
            out[name];
        } else if (!name.empty()) {
            out[name] += std::to_string(code) + "=" + value + ";";
        }
    }
    return out;
}

// The codes of the groups that differ between two versions of a record; -1 when their shape does
std::set<int> differing_codes(const std::vector<dxf_structure::Group>& a, const std::vector<dxf_structure::Group>& b) {
    std::set<int> out;
    if (a.size() != b.size()) {
        out.insert(-1);
        return out;
    }
    for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) out.insert(a[i].first);
    return out;
}

struct Delta {
    std::set<std::string> changed, added, removed;      // keys (handles)
    std::map<std::string, std::set<int>> codes;         // changed record: the codes that differ
    std::map<std::string, std::string> type_of;         // key: the record's type
    std::set<std::string> header;                       // the header variables that differ
};

Delta delta_of(const std::vector<Rec>& before, const std::vector<Rec>& after) {
    Delta d;
    std::vector<Keyed> a = keyed_records(before);
    std::vector<Keyed> b = keyed_records(after);
    std::map<std::string, const Rec*> in_before, in_after;
    for (const auto& k : a) { in_before[k.key] = k.record; d.type_of[k.key] = k.record->type(); }
    for (const auto& k : b) { in_after[k.key] = k.record; d.type_of[k.key] = k.record->type(); }
    for (const auto& [key, rec] : in_before) {
        auto it = in_after.find(key);
        if (it == in_after.end()) {
            d.removed.insert(key);
        } else if (key == "HEADER") {
            auto x = header_variables(*rec);
            auto y = header_variables(*it->second);
            for (const auto& [name, value] : x) if (!y.count(name) || y[name] != value) d.header.insert(name);
            for (const auto& [name, value] : y) if (!x.count(name)) d.header.insert(name);
        } else if (rec->groups != it->second->groups) {
            d.changed.insert(key);
            d.codes[key] = differing_codes(rec->groups, it->second->groups);
        }
    }
    for (const auto& [key, rec] : in_after) if (!in_before.count(key)) d.added.insert(key);
    return d;
}

std::string list_of(const Delta& d, const std::set<std::string>& keys) {
    std::string out;
    for (const auto& k : keys) {
        std::string codes;
        auto it = d.codes.find(k);
        if (it != d.codes.end()) for (int c : it->second) codes += (codes.empty() ? " codes " : ",") + std::to_string(c);
        out += (out.empty() ? "" : ", ") + k + "(" + d.type_of.at(k) + codes + ")";
    }
    return out.empty() ? "none" : out;
}

uint64_t hex_value(const std::string& text) {
    return std::strtoull(text.c_str(), nullptr, 16);
}

std::string seed_of(const std::vector<Rec>& recs) {
    for (const auto& r : recs) {
        if (r.type() != "SECTION" || r.value(2) != "HEADER") continue;
        for (size_t i = 0; i + 1 < r.groups.size(); ++i) {
            if (r.groups[i] == dxf_structure::Group{9, "$HANDSEED"}) return r.groups[i + 1].second;
        }
    }
    return {};
}

uint64_t max_handle_of(const std::vector<Rec>& recs) {
    uint64_t max = 0;
    for (const auto& k : keyed_records(recs)) {
        if (k.key == "HEADER") continue;
        std::string h = handle_of(*k.record);
        if (!h.empty()) max = std::max(max, hex_value(h));
    }
    return max;
}

// What is wrong with the links of a file: a handle used twice, a $HANDSEED not above the handles, a pointer to
// a record that is not there (0 is none). A file read can have such problems already; a change must not add one
std::set<std::string> problems_of(const std::vector<Rec>& recs) {
    std::set<std::string> out;
    std::set<uint64_t> handles;
    for (const auto& k : keyed_records(recs)) {
        if (k.key == "HEADER") continue;
        std::string h = handle_of(*k.record);
        if (h.empty()) continue;
        if (!handles.insert(hex_value(h)).second) out.insert("handle " + h + " used twice");
    }
    uint64_t max = handles.empty() ? 0 : *handles.rbegin();
    std::string seed = seed_of(recs);
    if (!seed.empty() && hex_value(seed) <= max) out.insert("$HANDSEED " + seed + " not above the handles");
    static const std::set<int> pointers = {330, 331, 340, 342, 343, 344, 350, 360, 390};
    for (const auto& k : keyed_records(recs)) {
        const Rec& r = *k.record;
        if (k.key == "HEADER" || r.section == "CLASSES") continue;
        for (const auto& [code, value] : r.groups) {
            if (code >= 1000) break;
            if (!pointers.count(code) || value == "0") continue;
            if (!handles.count(hex_value(value))) out.insert(r.type() + " " + k.key + " " + std::to_string(code) + " -> " + value);
        }
    }
    return out;
}

// One run of a change: the file loaded, saved as it is (what it said), loaded again, changed, saved
struct Run {
    Source source;
    DxfFormat format;
    std::vector<Rec> before, after;
    Delta delta;
    std::set<std::string> problems_before, problems_after;
    std::string what;
};

std::string join(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) out += (out.empty() ? "" : "; ") + s;
    return out;
}

// Runs `change` on every file (in both formats) and gives the result to `check`, which returns what is wrong.
// `change` returns false for a file it does not fit. The invariants of every change are checked here
// (the links, $HANDSEED, the file written again, the other format the same groups). Returns the files it ran on
int run_change(const std::string& name, Failures& failures, bool keeps_broken_links,
               const std::function<bool(hdxf::Drawing&)>& change,
               const std::function<std::vector<std::string>(const Run&)>& check) {
    int ran = 0;
    for (const Source& s : older_files()) {
        for (DxfFormat format : {s.format, other(s.format)}) {
            Run run;
            run.source = s;
            run.format = format;
            run.what = name + ": " + s.label + " " + name_of(format);
            hdxf::Drawing original;
            if (!original.load(s.path)) {
                failures.add(run.what + ": load: " + original.last_error());
                continue;
            }
            std::vector<uint8_t> unchanged = original.save_to_memory(format);
            hdxf::Drawing d;
            if (unchanged.empty() || !d.load(s.path)) {
                failures.add(run.what + ": the file unchanged: " + original.last_error());
                continue;
            }
            if (!change(d)) continue;
            ++ran;
            std::vector<uint8_t> out = d.save_to_memory(format);
            if (out.empty()) {
                failures.add(run.what + ": save: " + d.last_error());
                continue;
            }
            run.before = dxf_structure::records_of_file(unchanged);
            run.after = dxf_structure::records_of_file(out);
            run.delta = delta_of(run.before, run.after);
            run.problems_before = problems_of(run.before);
            run.problems_after = problems_of(run.after);

            std::vector<std::string> wrong = check(run);
            for (const auto& p : run.problems_after) {
                if (!run.problems_before.count(p)) wrong.push_back("a new problem: " + p);
            }
            if (keeps_broken_links) {
                for (const auto& p : run.problems_before) {
                    if (!run.problems_after.count(p) && p.find("$HANDSEED") == std::string::npos) {
                        wrong.push_back("a broken link of the file was changed: " + p);
                    }
                }
            }
            // $HANDSEED never lower than the file's
            std::string seed_before = seed_of(run.before), seed_after = seed_of(run.after);
            if (!seed_before.empty() && hex_value(seed_after) < hex_value(seed_before)) wrong.push_back("$HANDSEED lowered");
            for (const auto& h : run.delta.header) {
                if (h != "$HANDSEED") wrong.push_back("header variable " + h + " changed");
            }
            // The file read again is the same, written again the same bytes
            hdxf::Drawing again;
            if (!again.load_from_memory(out.data(), out.size())) {
                wrong.push_back("the file does not load: " + again.last_error());
            } else if (again.save_to_memory(format) != out) {
                wrong.push_back("the written file written again differs");
            }
            // The other format of the changed drawing: the same groups
            std::vector<uint8_t> converted = d.save_to_memory(other(format));
            if (converted.empty()) {
                wrong.push_back("the other format: " + d.last_error());
            } else {
                std::string why = group_difference(flat_groups(out), flat_groups(converted), format == DxfFormat::Ascii,
                                                   other(format) == DxfFormat::Ascii);
                if (!why.empty()) wrong.push_back("the other format: " + why);
            }
            if (!wrong.empty()) failures.add(run.what + ": " + join(wrong));
        }
    }
    return ran;
}

hdxf::Entity* first_of(hdxf::Drawing& d, hdxf::EntityType type) {
    for (auto& e : d.entities()) {
        if (e.type == type && !e.props.handle.empty()) return &e;
    }
    return nullptr;
}

std::vector<std::string> expect_only(const Run& run, const std::set<std::string>& changed, const std::set<std::string>& added,
                                     const std::set<std::string>& removed) {
    std::vector<std::string> wrong;
    if (run.delta.changed != changed) wrong.push_back("changed: " + list_of(run.delta, run.delta.changed) + " (expected " +
                                                      std::to_string(changed.size()) + ")");
    if (run.delta.added != added) wrong.push_back("added: " + list_of(run.delta, run.delta.added) + " (expected " +
                                                  std::to_string(added.size()) + ")");
    if (run.delta.removed != removed) wrong.push_back("removed: " + list_of(run.delta, run.delta.removed) + " (expected " +
                                                      std::to_string(removed.size()) + ")");
    return wrong;
}

// An entity built in a drawing of its own, to put in a block
hdxf::Entity line_entity(hdxf::Point3 a, hdxf::Point3 b) {
    hdxf::Drawing scratch;
    return scratch.add_line(a, b);
}

} // namespace

// A LINE moved: that record, and in it the group of the end point
TEST(roundtrip_r13_r14_change_a_line) {
    Failures failures;
    std::string handle;
    int ran = run_change("LINE", failures, true,
        [&](hdxf::Drawing& d) {
            hdxf::Entity* line = first_of(d, hdxf::EntityType::Line);
            if (!line) return false;
            handle = upper_case(line->props.handle);
            line->as_line().end.x += 1.5;
            return true;
        },
        [&](const Run& run) {
            std::vector<std::string> wrong = expect_only(run, {handle}, {}, {});
            auto it = run.delta.codes.find(handle);
            if (it != run.delta.codes.end() && it->second != std::set<int>({11})) wrong.push_back("other groups than 11 changed");
            return wrong;
        });
    if (ran == 0) hdxf_test::skip("no file with a LINE");
    failures.check();
}

// A dimension style changed: the record, and in it the one group (DIMSCALE 40, DIMTXT 140)
TEST(roundtrip_r13_r14_change_a_dimension_style) {
    Failures failures;
    std::string handle;
    int ran = run_change("DIMSTYLE", failures, true,
        [&](hdxf::Drawing& d) {
            for (auto& ds : d.dim_styles()) {
                if (ds.handle.empty()) continue;
                handle = upper_case(ds.handle);
                ds.dimscale += 1.0;
                ds.dimtxt += 0.5;
                return true;
            }
            return false;
        },
        [&](const Run& run) {
            std::vector<std::string> wrong = expect_only(run, {handle}, {}, {});
            auto it = run.delta.codes.find(handle);
            if (it != run.delta.codes.end() && it->second != std::set<int>({40, 140})) wrong.push_back("other groups than 40 and 140 changed");
            return wrong;
        });
    if (ran == 0) hdxf_test::skip("no file with a dimension style");
    failures.check();
}

// A text changed: the groups of the text (1, and 3 when a long MTEXT is in parts) of that record only
TEST(roundtrip_r13_r14_change_a_text) {
    Failures failures;
    std::string handle;
    int ran = run_change("TEXT", failures, true,
        [&](hdxf::Drawing& d) {
            for (auto& e : d.entities()) {
                if (e.props.handle.empty()) continue;
                if (e.type == hdxf::EntityType::Text) {
                    e.as_text().content += " (changed)";
                } else if (e.type == hdxf::EntityType::MText && e.as_mtext().content.size() < 120) {
                    e.as_mtext().content += " (changed)";
                } else {
                    continue;
                }
                handle = upper_case(e.props.handle);
                return true;
            }
            return false;
        },
        [&](const Run& run) {
            std::vector<std::string> wrong = expect_only(run, {handle}, {}, {});
            auto it = run.delta.codes.find(handle);
            if (it != run.delta.codes.end() && it->second != std::set<int>({1})) wrong.push_back("other groups than 1 changed");
            return wrong;
        });
    if (ran == 0) hdxf_test::skip("no file with a text");
    failures.check();
}

// A polyline's vertices: one added (a new VERTEX before the SEQEND, the others and the polyline as they were), the
// whole list replaced (the old VERTEX records gone, the new written, the polyline and its SEQEND as they were)
TEST(roundtrip_r13_r14_change_a_polyline_vertex_list) {
    Failures failures;
    std::string polyline, seqend, last_vertex;
    int ran = run_change("VERTEX added", failures, true,
        [&](hdxf::Drawing& d) {
            for (auto& e : d.entities()) {
                if (e.type != hdxf::EntityType::Polyline || e.props.handle.empty()) continue;
                auto& p = e.as_polyline();
                if (p.vertices.size() < 2 || p.is_polyface_mesh() || p.is_polygon_mesh() || p.seqend_handle.empty()) continue;
                polyline = upper_case(e.props.handle);
                seqend = upper_case(p.seqend_handle);
                last_vertex = upper_case(p.vertices.back().handle);
                hdxf::Vertex v = p.vertices.back();
                v.handle.clear();
                v.raw = hdxf::RawRecord();
                v.position.x += 3.0;
                p.vertices.push_back(v);
                return true;
            }
            return false;
        },
        [&](const Run& run) {
            std::vector<std::string> wrong;
            if (!run.delta.changed.empty()) wrong.push_back("changed: " + list_of(run.delta, run.delta.changed));
            if (!run.delta.removed.empty()) wrong.push_back("removed: " + list_of(run.delta, run.delta.removed));
            if (run.delta.added.size() != 1 || run.delta.type_of.at(*run.delta.added.begin()) != "VERTEX") {
                wrong.push_back("added: " + list_of(run.delta, run.delta.added));
                return wrong;
            }
            // Between the last vertex and the SEQEND
            const std::string added = *run.delta.added.begin();
            for (size_t i = 0; i < run.after.size(); ++i) {
                if (upper_case(handle_of(run.after[i])) != added) continue;
                if (i == 0 || upper_case(handle_of(run.after[i - 1])) != last_vertex) wrong.push_back("not after the last vertex");
                if (i + 1 >= run.after.size() || upper_case(handle_of(run.after[i + 1])) != seqend) wrong.push_back("not before the SEQEND");
                // Family A2000 has an owner on its VERTEX: the polyline
                const Rec& before_last = *std::find_if(run.before.begin(), run.before.end(),
                    [&](const Rec& r) { return upper_case(handle_of(r)) == last_vertex; });
                bool has_owner = false, new_has_owner = false;
                for (const auto& g : before_last.groups) has_owner = has_owner || (g.first == 330 && upper_case(g.second) == polyline);
                for (const auto& g : run.after[i].groups) new_has_owner = new_has_owner || (g.first == 330 && upper_case(g.second) == polyline);
                if (has_owner != new_has_owner) wrong.push_back("the new vertex's owner is not as the others'");
            }
            // $HANDSEED above the new handle, the one after the file's
            if (hex_value(seed_of(run.after)) != hex_value(seed_of(run.before)) + 1 &&
                hex_value(seed_of(run.before)) > max_handle_of(run.before)) {
                wrong.push_back("$HANDSEED is not the file's plus the new handle");
            }
            return wrong;
        });
    if (ran == 0) hdxf_test::skip("no file with a polyline");
    failures.check();

    Failures replaced;
    std::set<std::string> old_vertices;
    std::string seqend_handle;
    ran = run_change("VERTEX list replaced", replaced, true,
        [&](hdxf::Drawing& d) {
            for (auto& e : d.entities()) {
                if (e.type != hdxf::EntityType::Polyline || e.props.handle.empty()) continue;
                auto& p = e.as_polyline();
                if (p.vertices.size() < 2 || p.is_polyface_mesh() || p.is_polygon_mesh() || p.is_3d_polyline() ||
                    p.seqend_handle.empty()) continue;
                old_vertices.clear();
                for (const auto& v : p.vertices) old_vertices.insert(upper_case(v.handle));
                seqend_handle = upper_case(p.seqend_handle);
                hdxf::Vertex first = p.vertices.front();
                first.handle.clear();
                first.raw = hdxf::RawRecord();
                p.vertices.clear();
                for (int i = 0; i < 3; ++i) {
                    hdxf::Vertex v = first;
                    v.position = {10.0 * i, 5.0 * i * i, first.position.z};
                    p.vertices.push_back(v);
                }
                return true;
            }
            return false;
        },
        [&](const Run& run) {
            std::vector<std::string> wrong;
            if (run.delta.removed != old_vertices) wrong.push_back("removed: " + list_of(run.delta, run.delta.removed));
            if (!run.delta.changed.empty()) wrong.push_back("changed: " + list_of(run.delta, run.delta.changed));
            int vertices = 0;
            for (const auto& k : run.delta.added) vertices += run.delta.type_of.at(k) == "VERTEX";
            if (vertices != 3 || run.delta.added.size() != 3) wrong.push_back("added: " + list_of(run.delta, run.delta.added));
            if (run.delta.removed.count(seqend_handle)) wrong.push_back("the SEQEND went with the vertices");
            return wrong;
        });
    if (ran == 0) hdxf_test::skip("no file with a polyline");
    replaced.check();
}

// An MLINE added and its vertex list shortened: written whole (the count 72 and the groups of the vertices that
// are left), the rest of the record as it was
TEST(roundtrip_r13_r14_change_an_mline_vertex_list) {
    Failures failures;
    int ran = 0;
    for (const Source& s : older_files()) {
        if (is_unterminated(s.bytes)) continue;
        for (DxfFormat format : {s.format, other(s.format)}) {
            const std::string what = s.label + " " + name_of(format);
            hdxf::Drawing d;
            if (!d.load(s.path)) {
                failures.add(what + ": load: " + d.last_error());
                continue;
            }
            bool has_style = false;
            for (const auto& o : d.objects()) has_style = has_style || o.type == hdxf::ObjectType::MlineStyle;
            if (!has_style) continue;
            ++ran;
            d.add_mline({{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {20, 10, 0}}, false, 1.0, 0, "STANDARD");
            std::vector<uint8_t> first = d.save_to_memory(format);
            if (first.empty()) {
                failures.add(what + ": save: " + d.last_error());
                continue;
            }
            hdxf::Drawing two;
            if (!two.load_from_memory(first.data(), first.size())) {
                failures.add(what + ": " + two.last_error());
                continue;
            }
            std::string handle;
            for (auto& e : two.entities()) {
                if (e.type != hdxf::EntityType::Mline) continue;
                handle = upper_case(e.props.handle);
                e.as_mline().vertices.pop_back();
            }
            std::vector<uint8_t> second = two.save_to_memory(format);
            if (second.empty()) {
                failures.add(what + ": second save: " + two.last_error());
                continue;
            }
            std::vector<Rec> a = dxf_structure::records_of_file(first);
            std::vector<Rec> b = dxf_structure::records_of_file(second);
            Delta delta = delta_of(a, b);
            if (delta.changed != std::set<std::string>({handle}) || !delta.added.empty() || !delta.removed.empty()) {
                failures.add(what + ": changed " + list_of(delta, delta.changed));
                continue;
            }
            const Rec* before = nullptr;
            const Rec* after = nullptr;
            for (const auto& r : a) if (upper_case(handle_of(r)) == handle) before = &r;
            for (const auto& r : b) if (upper_case(handle_of(r)) == handle) after = &r;
            if (!before || !after) {
                failures.add(what + ": the MLINE is gone");
                continue;
            }
            // The groups of one vertex: 11-13 and 21-23 and 31-33, then per element 74 75 41 42
            const size_t per_vertex = (before->groups.size() - after->groups.size());
            if (std::atoi(before->value(72).c_str()) != 4 || std::atoi(after->value(72).c_str()) != 3 || per_vertex == 0) {
                failures.add(what + ": the vertex count is not 4 -> 3");
                continue;
            }
            // The first three vertices as they were: the groups from 11 on up to the cut
            size_t start = 0;
            while (start < before->groups.size() && before->groups[start].first != 11) ++start;
            bool same = true;
            for (size_t i = 0; i < start + (after->groups.size() - start); ++i) {
                if (i >= after->groups.size()) break;
                if (i == 0) continue;
                const auto& x = before->groups[i];
                const auto& y = after->groups[i];
                if (x.first == 72) continue;
                if (x != y) same = false;
            }
            if (!same) failures.add(what + ": the vertices that are left are not as they were");
        }
    }
    if (ran == 0) hdxf_test::skip("no file with an MLINESTYLE");
    failures.check();
}

// A block added with an INSERT of it: its BLOCK_RECORD, BLOCK, ENDBLK, entity and the INSERT are new, the table's
// count rises, nothing else is touched
TEST(roundtrip_r13_r14_add_a_block) {
    Failures failures;
    int ran = run_change("block added", failures, true,
        [&](hdxf::Drawing& d) {
            hdxf::Block& b = d.add_block("HDXF_NEW_BLOCK");
            b.entities.push_back(line_entity({0, 0, 0}, {3, 4, 0}));
            d.add_insert("HDXF_NEW_BLOCK", {5, 5, 0});
            return true;
        },
        [&](const Run& run) {
            std::vector<std::string> wrong;
            std::multiset<std::string> types;
            for (const auto& k : run.delta.added) types.insert(run.delta.type_of.at(k));
            std::multiset<std::string> expected = {"BLOCK_RECORD", "BLOCK", "LINE", "ENDBLK", "INSERT"};
            if (types != expected) wrong.push_back("added: " + list_of(run.delta, run.delta.added));
            if (!run.delta.removed.empty()) wrong.push_back("removed: " + list_of(run.delta, run.delta.removed));
            // The BLOCK_RECORD table: its count (70) one more, nothing else
            for (const auto& k : run.delta.changed) {
                if (run.delta.type_of.at(k) != "TABLE" || run.delta.codes.at(k) != std::set<int>({70})) {
                    wrong.push_back("changed: " + k + "(" + run.delta.type_of.at(k) + ")");
                }
            }
            if (run.delta.changed.size() != 1) wrong.push_back("the BLOCK_RECORD table is not the one record changed");
            // The new BLOCK_RECORD is named as the block
            bool named = false;
            for (const auto& k : run.delta.added) {
                for (const auto& r : run.after) {
                    if (upper_case(handle_of(r)) == k && r.type() == "BLOCK_RECORD") named = upper_case(r.value(2)) == "HDXF_NEW_BLOCK";
                }
            }
            if (!named) wrong.push_back("the BLOCK_RECORD has another name");
            return wrong;
        });
    if (ran == 0) hdxf_test::skip("no file");
    failures.check();
}

// The names of the blocks an INSERT in the entities or in another block uses (in capitals)
std::set<std::string> inserted_blocks(const hdxf::Drawing& d) {
    std::set<std::string> out;
    for (const auto& e : d.entities()) if (e.type == hdxf::EntityType::Insert) out.insert(upper_case(e.as_insert().block_name));
    for (const auto& b : d.blocks()) {
        for (const auto& e : b.entities) if (e.type == hdxf::EntityType::Insert) out.insert(upper_case(e.as_insert().block_name));
    }
    return out;
}

// A block removed with what refers to it (its INSERTs): its records go, with what is in them, and the extension
// dictionaries of them; no pointer is left to any of it. The table's count stays as the file had it
TEST(roundtrip_r13_r14_remove_a_block) {
    Failures failures;
    std::string name;
    std::set<std::string> gone;
    int ran = run_change("block removed", failures, true,
        [&](hdxf::Drawing& d) {
            std::set<std::string> used = inserted_blocks(d);
            for (const auto& b : d.blocks()) {
                if (b.name.empty() || b.name[0] == '*' || b.name[0] == '$' || b.record.handle.empty()) continue;
                if (used.count(upper_case(b.name))) continue;
                name = b.name;
                gone.clear();
                gone.insert(upper_case(b.record.handle));
                gone.insert(upper_case(b.handle));
                gone.insert(upper_case(b.endblk_handle));
                for (const auto& e : b.entities) {
                    gone.insert(upper_case(e.props.handle));
                    if (e.type == hdxf::EntityType::Polyline) {
                        for (const auto& v : e.as_polyline().vertices) gone.insert(upper_case(v.handle));
                        gone.insert(upper_case(e.as_polyline().seqend_handle));
                    } else if (e.type == hdxf::EntityType::Insert) {
                        for (const auto& a : e.as_insert().attributes) gone.insert(upper_case(a.props.handle));
                        gone.insert(upper_case(e.as_insert().seqend_handle));
                    }
                }
                gone.erase("");
                auto& blocks = d.blocks();
                blocks.erase(std::remove_if(blocks.begin(), blocks.end(),
                                            [&](const hdxf::Block& x) { return x.name == name; }), blocks.end());
                return true;
            }
            return false;
        },
        [&](const Run& run) {
            std::vector<std::string> wrong;
            // Everything of the block is gone (what else is gone is what hung on it: an extension dictionary
            // and its entries, which problems_of() sees as links to nowhere when it is not)
            for (const auto& h : gone) if (!run.delta.removed.count(h)) wrong.push_back(h + " of the block is still there");
            for (const auto& k : run.delta.removed) {
                if (gone.count(k)) continue;
                const std::string& t = run.delta.type_of.at(k);
                if (t != "DICTIONARY" && t != "XRECORD") wrong.push_back("removed besides the block: " + k + "(" + t + ")");
            }
            if (!run.delta.added.empty()) wrong.push_back("added: " + list_of(run.delta, run.delta.added));
            // What else changes is a record that pointed to something of the block (a reactor of a record outside
            // it, an entry of a dictionary); never the BLOCK_RECORD table's count (AutoCAD does not lower it)
            for (const auto& k : run.delta.changed) {
                const std::string& t = run.delta.type_of.at(k);
                if (t == "TABLE") wrong.push_back("the count of a table changed: " + k);
                else if (!points_to_any(run.before, k, run.delta.removed)) {
                    wrong.push_back("changed: " + list_of(run.delta, {k}) + " (removed block " + name + ")");
                }
            }
            return wrong;
        });
    if (ran == 0) hdxf_test::skip("no file with a block no INSERT uses");
    failures.check();
}

// A block with INSERTs removed together with them (the files with ATTRIBs: altera_*): the INSERT, its ATTRIBs and
// SEQEND are gone, the BLOCK_RECORD's list of INSERTs (where the file has one) does not name it
TEST(roundtrip_r13_r14_remove_an_insert_and_its_attributes) {
    Failures failures;
    std::set<std::string> gone;
    int ran = run_change("INSERT removed", failures, true,
        [&](hdxf::Drawing& d) {
            auto& ents = d.entities();
            for (auto& e : ents) {
                if (e.type != hdxf::EntityType::Insert || e.props.handle.empty()) continue;
                gone.clear();
                gone.insert(upper_case(e.props.handle));
                for (const auto& a : e.as_insert().attributes) gone.insert(upper_case(a.props.handle));
                if (!e.as_insert().attributes.empty()) gone.insert(upper_case(e.as_insert().seqend_handle));
                gone.erase("");
                std::string handle = e.props.handle;
                ents.erase(std::remove_if(ents.begin(), ents.end(),
                                          [&](const hdxf::Entity& x) { return x.props.handle == handle; }), ents.end());
                return true;
            }
            return false;
        },
        [&](const Run& run) {
            std::vector<std::string> wrong;
            if (run.delta.removed != gone) wrong.push_back("removed: " + list_of(run.delta, run.delta.removed));
            for (const auto& k : run.delta.changed) {
                if (run.delta.type_of.at(k) != "BLOCK_RECORD") wrong.push_back("changed: " + k + "(" + run.delta.type_of.at(k) + ")");
            }
            return wrong;
        });
    if (ran == 0) hdxf_test::skip("no file with an INSERT");
    failures.check();
}

// The GROUP objects of a file: a member removed (the group's list loses it, nothing else of the group changes), a
// group added (its GROUP, its entry in ACAD_GROUP, the members get it as reactor), a group removed (its entry
// goes, the members lose the reactor)
TEST(roundtrip_r13_r14_groups) {
    Failures removed_member;
    std::string member;
    std::set<std::string> in_groups;
    int ran = run_change("group member removed", removed_member, true,
        [&](hdxf::Drawing& d) {
            for (const auto& o : d.objects()) {
                if (o.type != hdxf::ObjectType::Group || o.as_group().entities.size() < 2) continue;
                member = upper_case(o.as_group().entities.front());
                in_groups.clear();
                for (const auto& g : d.objects()) {
                    if (g.type != hdxf::ObjectType::Group) continue;
                    for (const auto& m : g.as_group().entities) if (upper_case(m) == member) in_groups.insert(upper_case(g.handle));
                }
                auto& ents = d.entities();
                size_t before = ents.size();
                ents.erase(std::remove_if(ents.begin(), ents.end(),
                                          [&](const hdxf::Entity& e) { return upper_case(e.props.handle) == member; }), ents.end());
                return ents.size() != before;
            }
            return false;
        },
        [&](const Run& run) {
            std::vector<std::string> wrong;
            if (run.delta.removed != std::set<std::string>({member})) wrong.push_back("removed: " + list_of(run.delta, run.delta.removed));
            // (a HATCH that had the member as its boundary object loses the link to it)
            std::set<std::string> expected = in_groups;
            for (const auto& k : run.delta.changed) {
                if (run.delta.type_of.at(k) != "HATCH") continue;
                for (const auto& r : run.before) {
                    if (upper_case(handle_of(r)) != k) continue;
                    for (const auto& x : r.groups) if (x.first == 330 && upper_case(x.second) == member) expected.insert(k);
                }
            }
            if (run.delta.changed != expected) wrong.push_back("changed: " + list_of(run.delta, run.delta.changed));
            for (const auto& g : in_groups) {
                for (const auto& r : run.after) {
                    if (upper_case(handle_of(r)) != g) continue;
                    for (const auto& x : r.groups) if (x.first == 340 && upper_case(x.second) == member) wrong.push_back("the group still lists it");
                }
            }
            return wrong;
        });
    if (ran == 0) hdxf_test::skip("no file with a group");
    removed_member.check();

    Failures added;
    std::set<std::string> members;
    ran = run_change("group added", added, true,
        [&](hdxf::Drawing& d) {
            std::vector<hdxf::Entity*> two;
            for (auto& e : d.entities()) {
                if (e.type == hdxf::EntityType::Line && !e.props.handle.empty()) two.push_back(&e);
                if (two.size() == 2) break;
            }
            if (two.size() < 2) return false;
            members = {upper_case(two[0]->props.handle), upper_case(two[1]->props.handle)};
            d.add_group("HDXF_GROUP", two);
            return true;
        },
        [&](const Run& run) {
            std::vector<std::string> wrong;
            int groups = 0;
            for (const auto& k : run.delta.added) groups += run.delta.type_of.at(k) == "GROUP";
            if (groups != 1) wrong.push_back("added: " + list_of(run.delta, run.delta.added));
            for (const auto& m : members) if (!run.delta.changed.count(m)) wrong.push_back(m + " has no reactor");
            for (const auto& k : run.delta.changed) {
                if (!members.count(k) && run.delta.type_of.at(k) != "DICTIONARY") wrong.push_back("changed: " + k + "(" + run.delta.type_of.at(k) + ")");
            }
            if (!run.delta.removed.empty()) wrong.push_back("removed: " + list_of(run.delta, run.delta.removed));
            return wrong;
        });
    if (ran == 0) hdxf_test::skip("no file with LINEs");
    added.check();

    Failures group_removed;
    std::string group;
    std::set<std::string> its_members;
    ran = run_change("group removed", group_removed, true,
        [&](hdxf::Drawing& d) {
            auto& objects = d.objects();
            for (const auto& o : objects) {
                if (o.type != hdxf::ObjectType::Group || o.as_group().entities.empty()) continue;
                group = upper_case(o.handle);
                its_members.clear();
                for (const auto& m : o.as_group().entities) its_members.insert(upper_case(m));
                objects.erase(std::remove_if(objects.begin(), objects.end(),
                                             [&](const hdxf::Object& x) { return upper_case(x.handle) == group; }), objects.end());
                return true;
            }
            return false;
        },
        [&](const Run& run) {
            std::vector<std::string> wrong;
            if (run.delta.removed != std::set<std::string>({group})) wrong.push_back("removed: " + list_of(run.delta, run.delta.removed));
            if (!run.delta.added.empty()) wrong.push_back("added: " + list_of(run.delta, run.delta.added));
            bool dictionary = false;
            for (const auto& k : run.delta.changed) {
                const std::string& t = run.delta.type_of.at(k);
                if (t == "DICTIONARY") dictionary = true;
                else if (!its_members.count(k)) wrong.push_back("changed: " + k + "(" + t + ")");
            }
            if (!dictionary) wrong.push_back("ACAD_GROUP still has the entry");
            for (const auto& r : run.after) {
                for (const auto& x : r.groups) {
                    if (upper_case(x.second) == group && is_pointer_code(x.first)) wrong.push_back("a pointer to the group is left in " + r.type());
                }
            }
            return wrong;
        });
    if (ran == 0) hdxf_test::skip("no file with a group");
    group_removed.check();
}

// A LEADER with its annotation added, then the LEADER removed: the annotation has the leader as reactor while it is
// there and no pointer to it afterwards
TEST(roundtrip_r13_r14_leader_and_annotation) {
    Failures failures;
    int ran = 0;
    for (const Source& s : older_files()) {
        if (is_unterminated(s.bytes)) continue;
        for (DxfFormat format : {s.format, other(s.format)}) {
            const std::string what = s.label + " " + name_of(format);
            hdxf::Drawing d;
            if (!d.load(s.path)) {
                failures.add(what + ": load: " + d.last_error());
                continue;
            }
            ++ran;
            hdxf::Entity& note = d.add_mtext({30, 30, 0}, "note", 2.5);
            d.add_leader({{0, 0, 0}, {10, 10, 0}, {20, 10, 0}}, &note);
            std::vector<uint8_t> first = d.save_to_memory(format);
            if (first.empty()) {
                failures.add(what + ": save: " + d.last_error());
                continue;
            }
            hdxf::Drawing two;
            if (!two.load_from_memory(first.data(), first.size())) {
                failures.add(what + ": " + two.last_error());
                continue;
            }
            std::string leader, annotation;
            for (const auto& e : two.entities()) {
                if (e.type == hdxf::EntityType::Leader) {
                    leader = upper_case(e.props.handle);
                    annotation = upper_case(e.as_leader().annotation_handle);
                }
            }
            std::vector<Rec> a = dxf_structure::records_of_file(first);
            bool reactor = false;
            for (const auto& r : a) {
                if (upper_case(handle_of(r)) != annotation) continue;
                for (const auto& g : r.groups) reactor = reactor || (g.first == 330 && upper_case(g.second) == leader);
            }
            if (leader.empty() || !reactor) {
                failures.add(what + ": the annotation has not the leader as reactor");
                continue;
            }
            auto& ents = two.entities();
            ents.erase(std::remove_if(ents.begin(), ents.end(),
                                      [&](const hdxf::Entity& e) { return e.type == hdxf::EntityType::Leader; }), ents.end());
            std::vector<uint8_t> second = two.save_to_memory(format);
            if (second.empty()) {
                failures.add(what + ": second save: " + two.last_error());
                continue;
            }
            std::vector<Rec> b = dxf_structure::records_of_file(second);
            Delta delta = delta_of(a, b);
            std::vector<std::string> wrong;
            if (delta.removed != std::set<std::string>({leader})) wrong.push_back("removed: " + list_of(delta, delta.removed));
            if (delta.changed != std::set<std::string>({annotation})) wrong.push_back("changed: " + list_of(delta, delta.changed));
            for (const auto& r : b) {
                for (const auto& g : r.groups) {
                    if (upper_case(g.second) == leader && is_pointer_code(g.first)) wrong.push_back("a pointer to the leader in " + r.type());
                }
            }
            for (const auto& p : problems_of(b)) {
                if (!problems_of(a).count(p)) wrong.push_back("a new problem: " + p);
            }
            if (!wrong.empty()) failures.add(what + ": " + join(wrong));
        }
    }
    if (ran == 0) hdxf_test::skip("no R13/R14 test file");
    failures.check();
}

// An image added to an R14 file (an R13 file refuses it), then removed: its IMAGEDEF and IMAGEDEF_REACTOR are made and
// follow the image; the IMAGEDEF's reactors and ACAD_IMAGE_DICT follow the removal
TEST(roundtrip_r13_r14_image_added_and_removed) {
    Failures failures;
    int ran = 0;
    int refused = 0;
    for (const Source& s : older_files()) {
        if (is_unterminated(s.bytes)) continue;
        for (DxfFormat format : {s.format, other(s.format)}) {
            const std::string what = s.label + " " + name_of(format);
            hdxf::Drawing d;
            if (!d.load(s.path)) {
                failures.add(what + ": load: " + d.last_error());
                continue;
            }
            // R13 has no IMAGE (what a read R13 file refuses of new content is step 7.20's)
            if (d.version() == hdxf::DxfVersion::R13) {
                ++refused;
                continue;
            }
            d.add_image("C:\\bilder\\hdxf_ny.png", {0, 0, 0}, {200, 100}, 20.0);
            std::vector<uint8_t> first = d.save_to_memory(format);
            ++ran;
            if (first.empty()) {
                failures.add(what + ": save: " + d.last_error());
                continue;
            }
            std::vector<Rec> a = dxf_structure::records_of_file(first);
            std::string image, definition, reactor;
            hdxf::Drawing two;
            if (!two.load_from_memory(first.data(), first.size())) {
                failures.add(what + ": " + two.last_error());
                continue;
            }
            for (const auto& e : two.entities()) {
                if (e.type != hdxf::EntityType::Image) continue;
                image = upper_case(e.props.handle);
                definition = upper_case(e.as_image().image_def_handle);
                reactor = upper_case(e.as_image().reactor_handle);
            }
            const hdxf::Object* def = two.find_object(definition);
            bool listed = false;
            if (def) for (const auto& r : def->reactors) listed = listed || upper_case(r) == reactor;
            if (image.empty() || !def || !listed) {
                failures.add(what + ": the IMAGEDEF does not have the IMAGEDEF_REACTOR as reactor");
                continue;
            }
            auto& ents = two.entities();
            ents.erase(std::remove_if(ents.begin(), ents.end(),
                                      [&](const hdxf::Entity& e) { return e.type == hdxf::EntityType::Image; }), ents.end());
            std::vector<uint8_t> second = two.save_to_memory(format);
            if (second.empty()) {
                failures.add(what + ": second save: " + two.last_error());
                continue;
            }
            std::vector<Rec> b = dxf_structure::records_of_file(second);
            Delta delta = delta_of(a, b);
            std::vector<std::string> wrong;
            if (delta.removed != std::set<std::string>({image, reactor})) wrong.push_back("removed: " + list_of(delta, delta.removed));
            if (!delta.added.empty()) wrong.push_back("added: " + list_of(delta, delta.added));
            if (!delta.changed.count(definition)) wrong.push_back("the IMAGEDEF's reactors still list the IMAGEDEF_REACTOR");
            for (const auto& k : delta.changed) {
                if (delta.type_of.at(k) != "IMAGEDEF") wrong.push_back("changed: " + k + "(" + delta.type_of.at(k) + ")");
            }
            // The IMAGEDEF and the dictionary stay
            bool def_still = false;
            for (const auto& r : b) def_still = def_still || (upper_case(handle_of(r)) == definition && r.type() == "IMAGEDEF");
            if (!def_still) wrong.push_back("the IMAGEDEF went with its image");
            for (const auto& r : b) {
                for (const auto& g : r.groups) {
                    if ((upper_case(g.second) == image || upper_case(g.second) == reactor) && is_pointer_code(g.first)) {
                        wrong.push_back("a pointer to the image or its reactor in " + r.type());
                    }
                }
            }
            for (const auto& p : problems_of(b)) {
                if (!problems_of(a).count(p)) wrong.push_back("a new problem: " + p);
            }
            if (!wrong.empty()) failures.add(what + ": " + join(wrong));
        }
    }
    if (ran == 0 && refused == 0) hdxf_test::skip("no R13/R14 test file");
    failures.check();
}

// An IMAGEDEF removed from a file that has one: the entry of ACAD_IMAGE_DICT goes, its IMAGEDEF_REACTORs go; what
// the file already had broken (an IMAGEDEF_REACTOR of an image that is not in the file) stays as it was
TEST(roundtrip_r13_r14_image_definition_removed) {
    Failures failures;
    std::string definition;
    int ran = run_change("IMAGEDEF removed", failures, false,
        [&](hdxf::Drawing& d) {
            auto& objects = d.objects();
            for (const auto& o : objects) {
                if (o.type != hdxf::ObjectType::ImageDef) continue;
                definition = upper_case(o.handle);
                objects.erase(std::remove_if(objects.begin(), objects.end(),
                                             [&](const hdxf::Object& x) { return upper_case(x.handle) == definition; }), objects.end());
                return true;
            }
            return false;
        },
        [&](const Run& run) {
            std::vector<std::string> wrong;
            if (!run.delta.removed.count(definition)) wrong.push_back("the IMAGEDEF is still there");
            for (const auto& k : run.delta.removed) {
                const std::string& t = run.delta.type_of.at(k);
                if (k != definition && t != "IMAGEDEF_REACTOR") wrong.push_back("removed: " + k + "(" + t + ")");
            }
            for (const auto& r : run.after) {
                for (const auto& g : r.groups) {
                    if (upper_case(g.second) == definition && is_pointer_code(g.first)) wrong.push_back("a pointer to the IMAGEDEF in " + r.type());
                }
            }
            return wrong;
        });
    if (ran == 0) hdxf_test::skip("no file with an IMAGEDEF");
    failures.check();
}

namespace {

// A SAT 1.5 body (the plane face of test_acis_r13_r14.cpp), enciphered as AutoCAD writes it
hdxf::AcisData small_body() {
    const char* sat[] = {"105 10 1 0",
                         "-0 body $-1 $1 $-1 $-1 #",
                         "-1 lump $-1 $-1 $2 $0 #",
                         "-2 shell $-1 $-1 $-1 $3 $1 #",
                         "-3 face $-1 $-1 $4 $2 $-1 $5 reversed double out #",
                         "-4 loop $-1 $-1 $6 $3 #",
                         "-5 plane-surface $-1 0 0 2 0 0 1 1 0 0 0 I I I I #",
                         "-6 coedge $-1 $6 $6 $-1 $7 1 $4 $-1 #",
                         "-7 edge $-1 $8 $8 $6 $9 forward #",
                         "-8 vertex $-1 $7 $10 #",
                         "-9 ellipse-curve $-1 3 4 2 0 0 1 2 0 0 0.5 I I #",
                         "-10 point $-1 5 4 2 #"};
    hdxf::AcisData data;
    for (const char* line : sat) {
        std::string enciphered;
        for (const char* p = line; *p; ++p) {
            unsigned char c = static_cast<unsigned char>(*p);
            enciphered += (c > 32 && c < 159) ? static_cast<char>(159 - c) : static_cast<char>(c);
        }
        data.lines.push_back(enciphered);
    }
    return data;
}

} // namespace

// A body added (its SAT stream as given) and removed
TEST(roundtrip_r13_r14_acis_body_added_and_removed) {
    Failures failures;
    int ran = 0;
    for (const Source& s : older_files()) {
        if (is_unterminated(s.bytes)) continue;
        for (DxfFormat format : {s.format, other(s.format)}) {
            const std::string what = s.label + " " + name_of(format);
            hdxf::Drawing d;
            if (!d.load(s.path)) {
                failures.add(what + ": load: " + d.last_error());
                continue;
            }
            ++ran;
            hdxf::AcisData body = small_body();
            d.add_acis(hdxf::EntityType::Solid3D, body);
            std::vector<uint8_t> first = d.save_to_memory(format);
            if (first.empty()) {
                failures.add(what + ": save: " + d.last_error());
                continue;
            }
            hdxf::Drawing two;
            if (!two.load_from_memory(first.data(), first.size())) {
                failures.add(what + ": " + two.last_error());
                continue;
            }
            std::string handle;
            for (const auto& e : two.entities()) {
                if (e.type != hdxf::EntityType::Solid3D) continue;
                handle = upper_case(e.props.handle);
                if (e.as_acis().lines != body.lines) failures.add(what + ": the SAT stream is not as given");
            }
            if (handle.empty()) {
                failures.add(what + ": no 3DSOLID after the save");
                continue;
            }
            auto& ents = two.entities();
            ents.erase(std::remove_if(ents.begin(), ents.end(),
                                      [&](const hdxf::Entity& e) { return e.type == hdxf::EntityType::Solid3D; }), ents.end());
            std::vector<uint8_t> second = two.save_to_memory(format);
            if (second.empty()) {
                failures.add(what + ": second save: " + two.last_error());
                continue;
            }
            std::vector<Rec> a = dxf_structure::records_of_file(first);
            std::vector<Rec> b = dxf_structure::records_of_file(second);
            Delta delta = delta_of(a, b);
            std::vector<std::string> wrong;
            if (delta.removed != std::set<std::string>({handle})) wrong.push_back("removed: " + list_of(delta, delta.removed));
            if (!delta.added.empty()) wrong.push_back("added: " + list_of(delta, delta.added));
            for (const auto& p : problems_of(b)) {
                if (!problems_of(a).count(p)) wrong.push_back("a new problem: " + p);
            }
            if (!wrong.empty()) failures.add(what + ": " + join(wrong));
        }
    }
    if (ran == 0) hdxf_test::skip("no R13/R14 test file");
    failures.check();
}

// The older helper branches of a file (ACAD_LAYOUT, ACAD_PLOTSTYLENAME and the header round-trip XRECORDs of the files
// AutoCAD 2002 … 2005 saved as R13/R14, the proxy objects of ODA's) are as they were after changes elsewhere
TEST(roundtrip_r13_r14_helper_branches_stay_outside_a_change) {
    Failures failures;
    int with_helpers = 0;
    for (const Source& s : older_files()) {
        if (is_unterminated(s.bytes)) continue;
        hdxf::Drawing original;
        if (!original.load(s.path)) continue;
        std::vector<uint8_t> unchanged = original.save_to_memory(s.format);
        std::vector<Rec> before = dxf_structure::records_of_file(unchanged);
        // The records the root dictionary reaches through the helper names (and what those reach)
        std::map<std::string, const Rec*> by_handle;
        for (const auto& r : before) if (r.section == "OBJECTS" && !handle_of(r).empty()) by_handle[upper_case(handle_of(r))] = &r;
        std::set<std::string> helpers;
        std::vector<std::string> todo;
        for (const auto& r : before) {
            if (r.section != "OBJECTS" || r.type() != "DICTIONARY") continue;
            std::string name;
            for (const auto& [code, value] : r.groups) {
                if (code == 3) name = value;
                if ((code == 350 || code == 360) && (name == "ACAD_LAYOUT" || name == "ACAD_PLOTSTYLENAME" ||
                                                     name == "ACDBHEADERROUNDTRIPXREC")) {
                    todo.push_back(upper_case(value));
                }
            }
        }
        while (!todo.empty()) {
            std::string h = todo.back();
            todo.pop_back();
            if (!helpers.insert(h).second || !by_handle.count(h)) continue;
            const Rec& r = *by_handle[h];
            if (r.type() != "DICTIONARY" && r.type() != "ACDBDICTIONARYWDFLT") continue;
            for (const auto& [code, value] : r.groups) if (code == 350 || code == 360) todo.push_back(upper_case(value));
        }
        if (helpers.empty()) continue;
        ++with_helpers;

        hdxf::Drawing d;
        if (!d.load(s.path)) continue;
        if (hdxf::Entity* line = first_of(d, hdxf::EntityType::Line)) line->as_line().end.y += 2.0;
        d.add_line({0, 0, 0}, {1, 1, 0});
        hdxf::Block& block = d.add_block("HDXF_HELPER_BLOCK");
        block.entities.push_back(line_entity({0, 0, 0}, {1, 0, 0}));
        d.add_insert("HDXF_HELPER_BLOCK", {2, 2, 0});
        for (auto& ds : d.dim_styles()) {
            ds.dimscale += 1.0;
            break;
        }
        std::vector<uint8_t> out = d.save_to_memory(s.format);
        if (out.empty()) {
            failures.add(s.label + ": save: " + d.last_error());
            continue;
        }
        Delta delta = delta_of(before, dxf_structure::records_of_file(out));
        for (const auto& h : helpers) {
            if (delta.changed.count(h) || delta.removed.count(h)) failures.add(s.label + ": helper record " + h + " changed");
        }
    }
    if (with_helpers == 0) hdxf_test::skip("no file with ACAD_LAYOUT, ACAD_PLOTSTYLENAME or ACDBHEADERROUNDTRIPXREC");
    failures.check();
}

// What the file already had broken stays broken as it was (a pointer to a record that is not in the file, to
// something outside it), whatever else is written; none are made
TEST(roundtrip_r13_r14_broken_links_of_the_file_stay) {
    Failures failures;
    int with_broken = 0;
    for (const Source& s : older_files()) {
        if (is_unterminated(s.bytes)) continue;
        std::vector<Rec> in_file = dxf_structure::records_of_file(s.bytes);
        std::set<std::string> broken;
        for (const auto& p : problems_of(in_file)) if (p.find("$HANDSEED") == std::string::npos) broken.insert(p);
        if (broken.empty()) continue;
        ++with_broken;
        hdxf::Drawing d;
        if (!d.load(s.path)) continue;
        d.add_line({0, 0, 0}, {1, 1, 0});
        if (hdxf::Entity* line = first_of(d, hdxf::EntityType::Line)) line->as_line().end.y += 2.0;
        std::vector<uint8_t> out = d.save_to_memory(s.format);
        if (out.empty()) {
            failures.add(s.label + ": save: " + d.last_error());
            continue;
        }
        std::set<std::string> now = problems_of(dxf_structure::records_of_file(out));
        for (const auto& p : broken) if (!now.count(p)) failures.add(s.label + ": a broken link was changed: " + p);
        for (const auto& p : now) if (!broken.count(p) && p.find("$HANDSEED") == std::string::npos) failures.add(s.label + ": a new problem: " + p);
    }
    if (with_broken == 0) hdxf_test::skip("no file with a link that is broken already");
    failures.check();
}

// An entity's extension dictionary (an object the entity owns, with an XRECORD in it) goes with the entity when it is
// removed, in both families (the owner of an object is a 330 in family A2000, the reactor that holds it in family N)
TEST(roundtrip_r13_r14_extension_dictionary_follows_its_entity) {
    Failures failures;
    int ran = 0;
    for (const Source& s : older_files()) {
        if (is_unterminated(s.bytes)) continue;
        for (DxfFormat format : {s.format, other(s.format)}) {
            const std::string what = s.label + " " + name_of(format);
            hdxf::Drawing d;
            if (!d.load(s.path)) {
                failures.add(what + ": load: " + d.last_error());
                continue;
            }
            hdxf::Entity* line = first_of(d, hdxf::EntityType::Line);
            if (!line) continue;
            ++ran;
            const std::string owner = line->props.handle;
            // Handles far above the file's, so that none is used
            hdxf::Object dictionary;
            dictionary.type = hdxf::ObjectType::Dictionary;
            dictionary.handle = "F0000001";
            dictionary.owner = owner;
            dictionary.reactors = {owner};
            dictionary.data = hdxf::DictionaryData{false, 0, {{"HDXF_DATA", "F0000002", false}}};
            hdxf::Object record;
            record.type = hdxf::ObjectType::XRecord;
            record.handle = "F0000002";
            record.owner = "F0000001";
            record.reactors = {"F0000001"};
            hdxf::XRecordData data;
            data.cloning = 0;
            data.data = {{1, "text", 0}, {70, "5", 0}};
            record.data = data;
            d.objects().push_back(dictionary);
            d.objects().push_back(record);
            line->props.xdictionary = "F0000001";
            std::vector<uint8_t> first = d.save_to_memory(format);
            if (first.empty()) {
                failures.add(what + ": save: " + d.last_error());
                continue;
            }
            hdxf::Drawing two;
            if (!two.load_from_memory(first.data(), first.size())) {
                failures.add(what + ": " + two.last_error());
                continue;
            }
            const hdxf::Object* read_dictionary = two.find_object("F0000001");
            const hdxf::Object* read_record = two.find_object("F0000002");
            if (!read_dictionary || !read_record || read_dictionary->owner != owner || read_record->owner != "F0000001") {
                failures.add(what + ": the extension dictionary has not its owner when read");
                continue;
            }
            auto& ents = two.entities();
            ents.erase(std::remove_if(ents.begin(), ents.end(),
                                      [&](const hdxf::Entity& e) { return e.props.handle == owner; }), ents.end());
            std::vector<uint8_t> second = two.save_to_memory(format);
            if (second.empty()) {
                failures.add(what + ": second save: " + two.last_error());
                continue;
            }
            std::vector<Rec> a = dxf_structure::records_of_file(first);
            std::vector<Rec> b = dxf_structure::records_of_file(second);
            Delta delta = delta_of(a, b);
            std::vector<std::string> wrong;
            if (delta.removed != std::set<std::string>({upper_case(owner), "F0000001", "F0000002"})) {
                wrong.push_back("removed: " + list_of(delta, delta.removed));
            }
            if (!delta.added.empty()) wrong.push_back("added: " + list_of(delta, delta.added));
            for (const auto& p : problems_of(b)) {
                if (!problems_of(a).count(p)) wrong.push_back("a new problem: " + p);
            }
            if (!wrong.empty()) failures.add(what + ": " + join(wrong));
        }
    }
    if (ran == 0) hdxf_test::skip("no file with a LINE");
    failures.check();
}

// A dimension added to a file read: the writer makes what it needs (the dimension's block, its layer DEFPOINTS, the
// standard records the file lacks) and every table's count (70) is the file's, raised by each record added to it,
// those the writer makes itself too; a dimension style the dimension names exists as a record
TEST(roundtrip_r13_r14_records_the_writer_adds_raise_the_table_counts) {
    Failures failures;
    int ran = run_change("dimension added", failures, false,
        [&](hdxf::Drawing& d) {
            hdxf::Entity& dim = d.add_dimension(hdxf::DimensionType::Aligned, {34, 38, 0}, {17, 19, 0});
            dim.as_dimension().def_point2 = {0, 0, 0};
            dim.as_dimension().def_point3 = {40, 30, 0};
            return true;
        },
        [&](const Run& run) {
            std::vector<std::string> wrong;
            std::map<std::string, int> added_records;
            for (const auto& k : run.delta.added) ++added_records[run.delta.type_of.at(k)];
            auto count_of = [](const std::vector<Rec>& recs, const std::string& table) {
                for (const auto& r : recs) if (r.type() == "TABLE" && r.value(2) == table) return std::atoi(r.value(70).c_str());
                return -1;
            };
            for (const char* table : {"VPORT", "LTYPE", "LAYER", "STYLE", "VIEW", "UCS", "APPID", "DIMSTYLE", "BLOCK_RECORD"}) {
                int was = count_of(run.before, table);
                int now = count_of(run.after, table);
                if (was < 0 || now < 0) continue;
                if (now != was + added_records[table]) {
                    wrong.push_back(std::string(table) + " count " + std::to_string(was) + " -> " + std::to_string(now) + ", " +
                                    std::to_string(added_records[table]) + " added");
                }
            }
            // The dimension names a style that is in the DIMSTYLE table
            std::string named;
            std::set<std::string> styles;
            for (const auto& r : run.after) {
                if (r.section == "TABLES" && r.type() == "DIMSTYLE") styles.insert(upper_case(r.value(2)));
                if (r.section == "ENTITIES" && r.type() == "DIMENSION") named = upper_case(r.value(3));
            }
            (void)named;
            for (const auto& r : run.after) {
                if (r.type() == "DIMENSION" && !r.value(3).empty() && !styles.count(upper_case(r.value(3)))) {
                    wrong.push_back("the dimension names the style " + r.value(3) + ", which has no record");
                }
            }
            return wrong;
        });
    if (ran == 0) hdxf_test::skip("no R13/R14 test file");
    failures.check();
}
