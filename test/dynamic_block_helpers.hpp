#pragma once

#include "test_framework.hpp"
#include "hdxf.hpp"
#include "type_version_matrix.hpp"
#include "typed_corpus.hpp"
#include "TypedRecordTypes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// The helpers of the step 9.9 tests (test_graph_block_base.cpp, test_dynamic_block_relations.cpp): the facit and corpus
// files, an independent reader of ASCII records, and the comparison of two records.

using hdxf::BlockGripLocationData;
using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::EvalGraphEdge;
using hdxf::EvalGraphNode;
using hdxf::EvaluationGraphData;
using hdxf::Object;
using hdxf::ObjectType;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

namespace {

using Group = std::pair<int, std::string>;
using Rec = std::vector<Group>;

const char* const kTags[] = {"2000", "2004", "2007", "2010", "2013", "2018"};
const DxfVersion kVersions[] = {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007,
                                DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018};

std::filesystem::path facit_path(const std::string& name) { return hdxf_test::source_path("doc/facit/" + name); }

std::vector<uint8_t> facit_bytes(const std::string& name) {
    std::vector<uint8_t> in = hdxf_test::read_file_bytes(facit_path(name));
    if (in.empty()) hdxf_test::skip("missing doc/facit/" + name);
    return in;
}

std::vector<uint8_t> external_bytes(const std::string& relative) {
    std::vector<uint8_t> in = hdxf_test::read_file_bytes(hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(relative));
    if (in.empty()) hdxf_test::skip("missing " + relative);
    return in;
}

Drawing load(const std::vector<uint8_t>& bytes, const std::string& what) {
    Drawing d;
    assert_true(d.load_from_memory(bytes.data(), bytes.size()), what + ": " + d.last_error());
    return d;
}

std::vector<uint8_t> save(Drawing& d, DxfFormat format, const std::string& what) {
    std::vector<uint8_t> out = d.save_to_memory(format);
    assert_true(!out.empty(), what + ": " + d.last_error());
    return out;
}

DxfFormat format_of(const std::vector<uint8_t>& bytes) {
    return bytes.size() > 18 && std::string(bytes.begin(), bytes.begin() + 18) == "AutoCAD Binary DXF" ? DxfFormat::Binary : DxfFormat::Ascii;
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r')) s.pop_back();
    return s;
}

// An independent reader of an ASCII file: every record as its groups (code, value), the 0 first
std::vector<Rec> ascii_records(const std::vector<uint8_t>& bytes) {
    std::vector<Rec> out;
    std::string text(bytes.begin(), bytes.end());
    std::istringstream in(text);
    std::string code_line, value_line;
    while (std::getline(in, code_line) && std::getline(in, value_line)) {
        if (!value_line.empty() && value_line.back() == '\r') value_line.pop_back();
        int code = std::atoi(code_line.c_str());
        if (code == 0) out.emplace_back();
        if (!out.empty()) out.back().push_back({code, value_line});
    }
    return out;
}

Rec record_with_handle(const std::vector<Rec>& all, const std::string& handle) {
    for (const Rec& r : all) {
        for (const Group& g : r) {
            if (g.first == 5) {
                if (trim(g.second) == handle) return r;
                break;
            }
        }
    }
    return {};
}

bool same_value(const std::string& a, const std::string& b) {
    std::string x = trim(a), y = trim(b);
    if (x == y) return true;
    char* ex = nullptr;
    char* ey = nullptr;
    double dx = std::strtod(x.c_str(), &ex);
    double dy = std::strtod(y.c_str(), &ey);
    if (x.empty() || y.empty() || *ex != 0 || *ey != 0) return false;
    return dx == dy || std::fabs(dx - dy) <= 1e-15 * std::max(std::fabs(dx), std::fabs(dy));
}

std::string same_record(const Rec& a, const Rec& b) {
    if (a.size() != b.size()) return "the records have " + std::to_string(a.size()) + " and " + std::to_string(b.size()) + " groups";
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].first != b[i].first || !same_value(a[i].second, b[i].second)) {
            return "group " + std::to_string(i) + ": " + std::to_string(a[i].first) + " \"" + trim(a[i].second) + "\" and " +
                   std::to_string(b[i].first) + " \"" + trim(b[i].second) + "\"";
        }
    }
    return {};
}

std::vector<const Object*> objects_of(const Drawing& d, ObjectType type) {
    std::vector<const Object*> out;
    for (const Object& o : d.objects()) if (o.type == type) out.push_back(&o);
    return out;
}

const Object* the_object(const Drawing& d, ObjectType type) {
    std::vector<const Object*> all = objects_of(d, type);
    assert_eq(1, static_cast<int>(all.size()), "one record of the type");
    return all[0];
}

bool is_base_type(ObjectType t) {
    return t == ObjectType::EvaluationGraph || t == ObjectType::BlockGripLocation || t == ObjectType::BlockLinearParameter ||
           t == ObjectType::BlockLinearGrip || t == ObjectType::BlockVisibilityParameter || t == ObjectType::BlockVisibilityGrip ||
           t == ObjectType::BlockScaleAction || t == ObjectType::BlockRepresentation || t == ObjectType::DynamicBlockPurgePreventer;
}

bool contains(const std::vector<uint8_t>& bytes, const std::string& text) {
    return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
}

std::string lower_path(const std::filesystem::path& p) {
    std::string s = p.generic_string();
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The files of the corpus that have one of the record names
std::vector<std::filesystem::path> files_with(const std::vector<std::string>& names) {
    std::vector<std::filesystem::path> files;
    auto scan = [&](const std::filesystem::path& root, bool recursive) {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec)) return;
        auto take = [&](const std::filesystem::path& p) {
            std::string ext = p.extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".dxf") return;
            std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(p);
            for (const std::string& n : names) {
                if (contains(bytes, n)) { files.push_back(p); return; }
            }
        };
        if (recursive) {
            for (auto it = std::filesystem::recursive_directory_iterator(root, ec); it != std::filesystem::recursive_directory_iterator();
                 it.increment(ec)) {
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
    // (the files of doc/facit that have them are the dynamic block originals: a scan of the whole folder is long)
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(hdxf_test::source_path("doc/facit"), ec); it != std::filesystem::directory_iterator();
         it.increment(ec)) {
        if (ec) break;
        const std::string name = it->path().filename().string();
        if (it->is_regular_file() && (name.find("dyn") != std::string::npos || name.find("blocks") != std::string::npos)) {
            std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(it->path());
            for (const std::string& n : names) if (contains(bytes, n)) { files.push_back(it->path()); break; }
        }
    }
    scan(hdxf_test::source_path("doc/facit/web"), true);
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    return files;
}

const std::vector<std::string> kRecordNames = {"ACAD_EVALUATION_GRAPH", "BLOCKGRIPLOCATIONCOMPONENT", "BLOCKLINEARPARAMETER",
                                               "BLOCKLINEARGRIP", "BLOCKVISIBILITYPARAMETER", "BLOCKVISIBILITYGRIP",
                                               "BLOCKSCALEACTION", "ACDB_BLOCKREPRESENTATION_DATA",
                                               "ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION"};

std::string dynbase(const char* tag, bool binary, const char* change = "") {
    return std::string("acad_") + tag + "_dxf_dynbase" + (*change ? std::string("_") + change : std::string()) + (binary ? "_bin" : "") + ".dxf";
}

} // namespace
