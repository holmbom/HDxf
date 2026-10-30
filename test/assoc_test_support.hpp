#pragma once

#include "test_framework.hpp"
#include "typed_corpus.hpp"
#include "hdxf.hpp"
#include "TypedRecordTypes.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <functional>
#include <set>
#include <string>
#include <vector>

// What the ACDBASSOC tests of step 9.10 share (plan 2026-10-27-fas-9-typer.md): the corpus files that have one of the
// types, a record's text in an ASCII file, and the comparison of two files that treats two texts of one double as equal.

namespace assoc_test {

inline const std::vector<std::string>& assoc_names() {
    static const std::vector<std::string> names = {
        "ACDBASSOCACTION", "ACDBASSOCNETWORK", "ACDBASSOCDEPENDENCY", "ACDBASSOCGEOMDEPENDENCY",
        "ACDBASSOCPERSSUBENTMANAGER", "ACDBPERSSUBENTMANAGER", "ACDBASSOCPATHACTIONPARAM", "ACDBASSOCVERTEXACTIONPARAM",
        "ACDBASSOCOSNAPPOINTREFACTIONPARAM", "ACDBASSOCEDGEACTIONPARAM", "ACDBASSOCEXTRUDEDSURFACEACTIONBODY",
        "ACDBASSOCLOFTEDSURFACEACTIONBODY", "ACDBASSOCREVOLVEDSURFACEACTIONBODY", "ACDBASSOCSWEPTSURFACEACTIONBODY",
        "ACDBASSOCPLANESURFACEACTIONBODY", "ACDBASSOCALIGNEDDIMACTIONBODY"};
    return names;
}

inline bool is_assoc_type(hdxf::ObjectType t) {
    using hdxf::ObjectType;
    switch (t) {
        case ObjectType::AssocAction: case ObjectType::AssocNetwork: case ObjectType::AssocDependency:
        case ObjectType::AssocGeomDependency: case ObjectType::AssocPersSubentManager: case ObjectType::PersSubentManager:
        case ObjectType::AssocPathActionParam: case ObjectType::AssocVertexActionParam:
        case ObjectType::AssocOsnapPointRefActionParam: case ObjectType::AssocEdgeActionParam:
        case ObjectType::AssocExtrudedSurfaceActionBody: case ObjectType::AssocLoftedSurfaceActionBody:
        case ObjectType::AssocRevolvedSurfaceActionBody: case ObjectType::AssocSweptSurfaceActionBody:
        case ObjectType::AssocPlaneSurfaceActionBody: case ObjectType::AssocAlignedDimActionBody:
            return true;
        default:
            return false;
    }
}

inline const char* dxf_name_of(hdxf::ObjectType t) {
    using hdxf::ObjectType;
    switch (t) {
        case ObjectType::AssocAction: return "ACDBASSOCACTION";
        case ObjectType::AssocNetwork: return "ACDBASSOCNETWORK";
        case ObjectType::AssocDependency: return "ACDBASSOCDEPENDENCY";
        case ObjectType::AssocGeomDependency: return "ACDBASSOCGEOMDEPENDENCY";
        case ObjectType::AssocPersSubentManager: return "ACDBASSOCPERSSUBENTMANAGER";
        case ObjectType::PersSubentManager: return "ACDBPERSSUBENTMANAGER";
        case ObjectType::AssocPathActionParam: return "ACDBASSOCPATHACTIONPARAM";
        case ObjectType::AssocVertexActionParam: return "ACDBASSOCVERTEXACTIONPARAM";
        case ObjectType::AssocOsnapPointRefActionParam: return "ACDBASSOCOSNAPPOINTREFACTIONPARAM";
        case ObjectType::AssocEdgeActionParam: return "ACDBASSOCEDGEACTIONPARAM";
        case ObjectType::AssocExtrudedSurfaceActionBody: return "ACDBASSOCEXTRUDEDSURFACEACTIONBODY";
        case ObjectType::AssocLoftedSurfaceActionBody: return "ACDBASSOCLOFTEDSURFACEACTIONBODY";
        case ObjectType::AssocRevolvedSurfaceActionBody: return "ACDBASSOCREVOLVEDSURFACEACTIONBODY";
        case ObjectType::AssocSweptSurfaceActionBody: return "ACDBASSOCSWEPTSURFACEACTIONBODY";
        case ObjectType::AssocPlaneSurfaceActionBody: return "ACDBASSOCPLANESURFACEACTIONBODY";
        case ObjectType::AssocAlignedDimActionBody: return "ACDBASSOCALIGNEDDIMACTIONBODY";
        default: return "";
    }
}

inline bool is_binary_bytes(const std::vector<uint8_t>& b) {
    static const char sentinel[] = "AutoCAD Binary DXF";
    return b.size() >= sizeof(sentinel) - 1 && std::equal(sentinel, sentinel + sizeof(sentinel) - 1, b.begin());
}

inline bool contains(const std::vector<uint8_t>& bytes, const std::string& needle) {
    std::vector<uint8_t> pattern(needle.begin(), needle.end());
    std::boyer_moore_horspool_searcher searcher(pattern.begin(), pattern.end());
    return std::search(bytes.begin(), bytes.end(), searcher) != bytes.end();
}

// The DXF files of the corpus that have one of the names: build/test-data, doc/facit and doc/acad_*. HDXF_ASSOC_ONLY=text
// keeps the files whose name has the text (to look at one file)
inline std::vector<std::filesystem::path> files_with(const std::vector<std::string>& names_in) {
    // every name starts with ACDBASSOC or ACDBPERSSUBENT: two searches tell whether a file has any of them; the answer is kept
    static std::map<std::string, std::vector<std::filesystem::path>> cache;
    const char* only_env = std::getenv("HDXF_ASSOC_ONLY");
    const std::string key = std::string(only_env ? only_env : "") + "|" + (names_in.empty() ? "" : names_in[0]);
    auto cached = cache.find(key);
    if (cached != cache.end()) return cached->second;
    std::vector<std::string> names = {"ACDBASSOC", "ACDBPERSSUBENT"};
    std::vector<std::filesystem::path> files;
    auto scan = [&](const std::filesystem::path& root, bool recursive) {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec)) return;
        auto take = [&](const std::filesystem::path& p) {
            std::string ext = p.extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".dxf") return;
            if (const char* only = std::getenv("HDXF_ASSOC_ONLY")) {
                if (p.filename().string().find(only) == std::string::npos) return;
            }
            std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(p);
            for (const std::string& n : names) {
                if (contains(bytes, n)) { files.push_back(p); return; }
            }
        };
        if (recursive) {
            for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
                 it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) break;
                if (it->is_regular_file()) take(it->path());
            }
        } else {
            for (auto it = std::filesystem::directory_iterator(root, ec); it != std::filesystem::directory_iterator(); it.increment(ec)) {
                if (ec) break;
                if (it->is_regular_file() && it->path().filename().string().rfind("acad_", 0) == 0) take(it->path());
            }
        }
    };
    scan(hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR), true);
    scan(hdxf_test::source_path("doc/facit"), true);
    scan(hdxf_test::source_path("doc"), false);
    std::sort(files.begin(), files.end());
    cache[key] = files;
    return files;
}

inline std::vector<uint8_t> bytes_of(const std::filesystem::path& p) { return hdxf_test::read_file_bytes(p); }

inline bool load_bytes(hdxf::Drawing& d, const std::vector<uint8_t>& bytes) {
    return !bytes.empty() && d.load_from_memory(bytes.data(), bytes.size());
}

inline bool load_file(hdxf::Drawing& d, const std::filesystem::path& path) { return load_bytes(d, bytes_of(path)); }

inline hdxf::Drawing reload(const std::vector<uint8_t>& bytes) {
    hdxf::Drawing r;
    bool ok = r.load_from_memory(bytes.data(), bytes.size());
    hdxf_test::assert_true(ok, "the saved drawing is not readable: " + r.last_error());
    return r;
}

// The text lines of a record of an ASCII file: from "  0\nTYPE\n  5\nHANDLE" to the next "  0"
inline std::string ascii_record(const std::vector<uint8_t>& bytes, const std::string& type, const std::string& handle) {
    std::string text(bytes.begin(), bytes.end());
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    std::string key = "  0\n" + type + "\n  5\n" + handle + "\n";
    size_t at = text.find(key);
    if (at == std::string::npos) return {};
    size_t end = text.find("\n  0\n", at + 4);
    return text.substr(at, end == std::string::npos ? std::string::npos : end + 1 - at);
}

// A binary file as ASCII, by reading it and writing it again (its preserved records, unchanged)
inline std::vector<uint8_t> as_ascii(const std::vector<uint8_t>& bytes) {
    hdxf::Drawing d;
    if (!d.load_from_memory(bytes.data(), bytes.size())) return {};
    return d.save_to_memory(hdxf::DxfFormat::Ascii);
}

inline std::vector<std::string> split_lines(const std::vector<uint8_t>& bytes) {
    std::vector<std::string> out;
    std::string cur;
    for (uint8_t c : bytes) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += static_cast<char>(c);
    }
    out.push_back(cur);
    return out;
}

// Two texts that are one double: AutoCAD writes a double's 16 digits cut off where HDxf rounds them
inline bool same_number(const std::string& a, const std::string& b) {
    auto number = [](const std::string& s, double& v) {
        if (s.find('.') == std::string::npos) return false;
        char* end = nullptr;
        v = std::strtod(s.c_str(), &end);
        return end && *end == '\0' && end != s.c_str();
    };
    double da = 0.0, db = 0.0;
    return number(a, da) && number(b, db) && da == db;
}

// The first line where two ASCII texts differ, with a little context; empty when they are the same
inline std::string first_difference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    std::vector<std::string> x = split_lines(a), y = split_lines(b);
    std::string header;
    for (size_t i = 0; i < x.size() && i < y.size(); ++i) {
        if (!x[i].empty() && x[i][0] == '$') header = x[i];
        if (x[i] == y[i]) continue;
        // a drawing that was changed names the last saver and has new GUIDs
        if (i >= 2 && (header == "$LASTSAVEDBY" || header == "$VERSIONGUID" || header == "$FINGERPRINTGUID") && x[i - 2] == header) continue;
        if (same_number(x[i], y[i])) continue;
        std::string out = "line " + std::to_string(i + 1) + ": \"" + x[i] + "\" against \"" + y[i] + "\" (before:";
        for (size_t k = i >= 6 ? i - 6 : 0; k < i; ++k) out += " [" + x[k] + "]";
        return out + ")";
    }
    if (x.size() == y.size()) return std::string();
    return "one has " + std::to_string(x.size()) + " lines, the other " + std::to_string(y.size());
}

inline std::string record_difference(const std::string& expected, const std::string& got) {
    std::vector<uint8_t> a(expected.begin(), expected.end()), b(got.begin(), got.end());
    return first_difference(a, b);
}

struct RecordId {
    std::string type;
    std::string handle;
};

inline std::vector<RecordId> assoc_record_ids(const hdxf::Drawing& d) {
    std::vector<RecordId> ids;
    for (const auto& o : d.objects()) {
        if (is_assoc_type(o.type)) ids.push_back({dxf_name_of(o.type), o.handle});
    }
    return ids;
}

// Empties the preserved record of every ACDBASSOC record: they are written from the typed values alone
inline int forget_assoc_records(hdxf::Drawing& d) {
    int n = 0;
    for (auto& o : d.objects()) {
        if (is_assoc_type(o.type)) { o.raw = hdxf::RawRecord(); ++n; }
    }
    return n;
}

// The index of the object with the handle, -1 when there is none
inline int object_index(const hdxf::Drawing& d, const std::string& handle) {
    for (size_t i = 0; i < d.objects().size(); ++i) {
        if (d.objects()[i].handle == handle) return static_cast<int>(i);
    }
    return -1;
}

inline const hdxf::Object* find_object(const hdxf::Drawing& d, const std::string& handle) {
    int i = object_index(d, handle);
    return i < 0 ? nullptr : &d.objects()[static_cast<size_t>(i)];
}

inline size_t count_objects(const hdxf::Drawing& d, hdxf::ObjectType type) {
    size_t n = 0;
    for (const auto& o : d.objects()) if (o.type == type) ++n;
    return n;
}

inline void erase_object(hdxf::Drawing& d, const std::string& handle) {
    int i = object_index(d, handle);
    hdxf_test::assert_true(i >= 0, "no object " + handle);
    d.objects().erase(d.objects().begin() + i);
}

} // namespace assoc_test
