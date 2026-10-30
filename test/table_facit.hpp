#pragma once

#include "hdxf.hpp"
#include "TableLayout.hpp"
#include "typed_corpus.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// Helpers of the table layout and graphics tests (phase 9, step 9.5): the AutoCAD-saved tables of the corpus, the fonts
// they need, and the comparison of what HDxf lays out with what AutoCAD saved — the geometry of the cells, the entities
// of the *T block, the commands of the proxy graphics. AutoCAD's own files are the only facit; what HDxf lays out is
// never compared with HDxf.

namespace hdxf_table {

using hdxf::Drawing;
using hdxf::Entity;
using hdxf::EntityType;
using hdxf::TableData;

// ---- the files

inline bool has_text(const std::vector<uint8_t>& bytes, const std::string& needle) {
    return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
}

// The files that have an ACAD_TABLE: build/test-data, doc/facit (AutoCAD 2025: the GUI and the core) and doc/. A file
// of the same bytes once. `facit_only`: only AutoCAD 2025's own (doc/facit, not the web files).
inline std::vector<std::filesystem::path> table_files(bool facit_only = false) {
    std::vector<std::filesystem::path> files;
    std::set<std::string> seen;
    auto take = [&](const std::filesystem::path& p) {
        std::string ext = p.extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".dxf") return;
        std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(p);
        if (bytes.empty() || !has_text(bytes, "ACAD_TABLE")) return;
        if (!seen.insert(hdxf_test::sha256_hex(bytes)).second) return;
        files.push_back(p);
    };
    auto scan = [&](const std::filesystem::path& root, bool recursive) {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec)) return;
        if (recursive) {
            for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
                 it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) break;
                if (it->is_regular_file()) take(it->path());
            }
        } else {
            for (auto it = std::filesystem::directory_iterator(root, ec); it != std::filesystem::directory_iterator(); it.increment(ec)) {
                if (ec) break;
                if (it->is_regular_file()) take(it->path());
            }
        }
    };
    if (!facit_only) scan(hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR), true);
    std::filesystem::path facit = hdxf_test::source_path("doc/facit");
    std::error_code ec;
    if (std::filesystem::exists(facit, ec)) {
        for (auto it = std::filesystem::directory_iterator(facit, ec); it != std::filesystem::directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (it->is_regular_file() && it->path().filename().string().rfind("acad_", 0) == 0) take(it->path());
        }
        if (!facit_only) scan(facit / "web", true);
    }
    std::sort(files.begin(), files.end());
    return files;
}

// Fonts: Windows' and, when installed, AutoCAD's (the corpus's SHX styles)
struct ScopedFonts {
    std::vector<std::filesystem::path> saved;
    ScopedFonts() {
        saved = hdxf::font_paths();
        std::vector<std::filesystem::path> paths = saved;
        std::filesystem::path acad = "C:/Program Files/Autodesk/AutoCAD 2025/Fonts";
        std::error_code ec;
        if (std::filesystem::exists(acad, ec)) paths.push_back(acad);
        hdxf::set_font_paths(paths);
    }
    ~ScopedFonts() { hdxf::set_font_paths(saved); }
};

inline std::filesystem::path windows_font(const char* name) {
    const char* windir = std::getenv("WINDIR");
    std::filesystem::path p = std::filesystem::path(windir ? windir : "C:/Windows") / "Fonts" / name;
    return p;
}

inline bool arial_available() {
    std::error_code ec;
    return std::filesystem::exists(windows_font("arial.ttf"), ec);
}

// ---- comparing what AutoCAD saved with what HDxf lays out

struct Tolerance {
    double absolute = 2e-6;     // AutoCAD's older files keep seven significant digits
    double relative = 1e-9;
    bool near(double a, double b) const { return std::fabs(a - b) <= absolute + relative * std::max(std::fabs(a), std::fabs(b)); }
    bool near(const hdxf::Point3& a, const hdxf::Point3& b) const { return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z); }
};

inline std::string point_text(const hdxf::Point3& p) {
    std::ostringstream o;
    o << "(" << p.x << "," << p.y << "," << p.z << ")";
    return o.str();
}

// The first difference of two entities of a *T block, or empty
inline std::string entity_difference(const Entity& a, const Entity& b, const Tolerance& tol = Tolerance(), bool transparency = true) {
    if (a.type != b.type) return "type " + std::to_string(static_cast<int>(a.type)) + " against " + std::to_string(static_cast<int>(b.type));
    if (a.props.layer != b.props.layer) return "layer " + a.props.layer + " against " + b.props.layer;
    if (a.props.color.aci != b.props.color.aci || a.props.color.rgb != b.props.color.rgb) {
        return "color " + std::to_string(a.props.color.aci) + "/" + std::to_string(a.props.color.rgb) + " against " +
               std::to_string(b.props.color.aci) + "/" + std::to_string(b.props.color.rgb);
    }
    if (a.props.visible != b.props.visible) return "visibility";
    if (transparency && a.props.transparency.raw != b.props.transparency.raw) return "transparency";
    switch (a.type) {
        case EntityType::Solid: {
            const auto& x = a.as_solid();
            const auto& y = b.as_solid();
            if (!tol.near(x.p0, y.p0) || !tol.near(x.p1, y.p1) || !tol.near(x.p2, y.p2) || !tol.near(x.p3, y.p3)) return "solid corners";
            return {};
        }
        case EntityType::MText: {
            const auto& x = a.as_mtext();
            const auto& y = b.as_mtext();
            if (x.content != y.content) return "text \"" + x.content + "\" against \"" + y.content + "\"";
            if (!tol.near(x.insertion_point, y.insertion_point)) return "text \"" + x.content + "\" at " + point_text(x.insertion_point) + " against " + point_text(y.insertion_point);
            if (!tol.near(x.height, y.height) || !tol.near(x.reference_width, y.reference_width)) {
                return "text \"" + x.content + "\" height/width " + std::to_string(x.height) + "/" + std::to_string(x.reference_width) + " against " +
                       std::to_string(y.height) + "/" + std::to_string(y.reference_width);
            }
            if (x.attachment != y.attachment || x.drawing_direction != y.drawing_direction) return "text attachment";
            if (x.style != y.style) return "text style";
            if (!tol.near(x.x_axis_direction, y.x_axis_direction)) return "text direction";
            return {};
        }
        case EntityType::Line: {
            const auto& x = a.as_line();
            const auto& y = b.as_line();
            if (!tol.near(x.start, y.start) || !tol.near(x.end, y.end)) {
                return "line " + point_text(x.start) + "-" + point_text(x.end) + " against " + point_text(y.start) + "-" + point_text(y.end);
            }
            if (a.props.lineweight != b.props.lineweight) return "lineweight";
            if (a.props.linetype != b.props.linetype) return "linetype";
            return {};
        }
        case EntityType::Insert: {
            const auto& x = a.as_insert();
            const auto& y = b.as_insert();
            if (x.block_name != y.block_name) return "insert of " + x.block_name + " against " + y.block_name;
            if (!tol.near(x.insertion_point, y.insertion_point)) return "insert " + x.block_name + " at " + point_text(x.insertion_point) + " against " + point_text(y.insertion_point);
            if (!tol.near(x.x_scale, y.x_scale) || !tol.near(x.y_scale, y.y_scale) || !tol.near(x.z_scale, y.z_scale) || !tol.near(x.rotation, y.rotation)) return "insert scale";
            if (x.attributes.size() != y.attributes.size()) return "insert attributes";
            return {};
        }
        default:
            return "an entity of a type the table block does not have";
    }
}

inline std::string entities_difference(const std::vector<Entity>& autocad, const std::vector<Entity>& ours, const Tolerance& tol = Tolerance(), bool transparency = true) {
    if (autocad.size() != ours.size()) return std::to_string(autocad.size()) + " entities against " + std::to_string(ours.size());
    for (size_t i = 0; i < autocad.size(); ++i) {
        std::string d = entity_difference(autocad[i], ours[i], tol, transparency);
        if (!d.empty()) return "entity " + std::to_string(i) + ": " + d;
    }
    return {};
}

// The first difference of two TABLEGEOMETRY: the sizes and the places of every cell's contents. AutoCAD 2025 writes flags 7, its older
// versions 3 for a laid out cell: both are laid out
inline std::string geometry_difference(const hdxf::TableGeometryData& a, const hdxf::TableGeometryData& b, const Tolerance& tol = Tolerance()) {
    if (a.rows != b.rows || a.columns != b.columns || a.cells.size() != b.cells.size()) return "the number of cells";
    for (size_t i = 0; i < a.cells.size(); ++i) {
        const auto& x = a.cells[i];
        const auto& y = b.cells[i];
        const std::string at = "cell " + std::to_string(i) + " (row " + std::to_string(i / static_cast<size_t>(a.columns)) + ", column " +
                               std::to_string(i % static_cast<size_t>(a.columns)) + "): ";
        const bool flags_equal = x.flags == y.flags || (x.flags == 3 && y.flags == 7);
        if (!flags_equal) return at + "flags " + std::to_string(x.flags) + " against " + std::to_string(y.flags);
        if (!tol.near(x.width, y.width) || !tol.near(x.height, y.height)) {
            return at + "size " + std::to_string(x.width) + " x " + std::to_string(x.height) + " against " + std::to_string(y.width) + " x " + std::to_string(y.height);
        }
        if (x.contents.size() != y.contents.size()) return at + std::to_string(x.contents.size()) + " contents against " + std::to_string(y.contents.size());
        for (size_t k = 0; k < x.contents.size(); ++k) {
            const auto& p = x.contents[k];
            const auto& q = y.contents[k];
            if (!tol.near(p.point, q.point) || !tol.near(p.anchor, q.anchor) || !tol.near(p.width, q.width) || !tol.near(p.height, q.height) ||
                !tol.near(p.content_width, q.content_width) || !tol.near(p.content_height, q.content_height) || p.flags != q.flags) {
                return at + "content " + std::to_string(k) + " at " + point_text(p.point) + "/" + point_text(p.anchor) + " against " + point_text(q.point) + "/" +
                       point_text(q.anchor);
            }
        }
    }
    return {};
}

// ---- the proxy graphics as commands

struct ProxyCommand {
    int type = 0;
    std::vector<uint8_t> data;
};

// skip_markers: no marker commands at all; skip_zero_markers: those with the value 0 (the text's; a field's is 2000000000)
inline std::vector<ProxyCommand> proxy_commands(const std::vector<uint8_t>& s, bool skip_markers = false, bool skip_zero_markers = false) {
    std::vector<ProxyCommand> out;
    auto at = [&](size_t o) {
        uint32_t u = 0;
        for (int i = 0; i < 4; ++i) u |= static_cast<uint32_t>(s[o + i]) << (8 * i);
        return u;
    };
    if (s.size() < 8) return out;
    const uint32_t count = at(4);
    size_t o = 8;
    for (uint32_t i = 0; i < count && o + 8 <= s.size(); ++i) {
        const uint32_t size = at(o), type = at(o + 4);
        if (size < 8 || o + size > s.size()) break;
        const bool zero_marker = type == 19 && size == 12 && at(o + 8) == 0;
        if (!(skip_markers && type == 19) && !(skip_zero_markers && zero_marker)) out.push_back({static_cast<int>(type), std::vector<uint8_t>(s.begin() + static_cast<long>(o + 8), s.begin() + static_cast<long>(o + size))});
        o += size;
    }
    return out;
}

// A TEXT command's strings are padded to four bytes with what AutoCAD's memory had: zeroed
inline void normalize_text_command(ProxyCommand& c) {
    if (c.type != 11 && c.type != 38) return;
    std::vector<uint8_t>& d = c.data;
    if (d.size() < 80) return;
    size_t o = 72;
    const bool wide = c.type == 38;
    auto zero_pad = [&]() {
        const size_t start = o;
        if (wide) {
            while (o + 1 < d.size() && !(d[o] == 0 && d[o + 1] == 0)) o += 2;
            o += 2;
        } else {
            while (o < d.size() && d[o] != 0) ++o;
            ++o;
        }
        const size_t end = start + ((o - start + 3) / 4) * 4;
        for (size_t k = o; k < end && k < d.size(); ++k) d[k] = 0;
        o = std::min(end, d.size());
    };
    zero_pad();
    o += 8 + 32 + 20;
    if (wide) o += 16;
    if (o >= d.size()) return;
    zero_pad();
    if (o >= d.size()) return;
    zero_pad();
    if (wide && o < d.size()) zero_pad();
}

inline bool command_equal(ProxyCommand a, ProxyCommand b) {
    if (a.type != b.type || a.data.size() != b.data.size()) return false;
    normalize_text_command(a);
    normalize_text_command(b);
    if (a.data == b.data) return true;
    // the padding after an empty font and big font name is whatever AutoCAD's memory had
    if ((a.type == 38 || a.type == 11) && a.data.size() > 12) {
        const size_t n = a.data.size();
        for (size_t k : {n - 6, n - 5, n - 2, n - 1}) { a.data[k] = 0; b.data[k] = 0; }
        if (a.data == b.data) return true;
    }
    // the doubles of a command that has them, to the last digits AutoCAD keeps (its older files, seven digits, and the
    // sums of its own arithmetic)
    const size_t off = (a.type == 6 || a.type == 7 || a.type == 9 || a.type == 32) ? 4 : 0;
    for (size_t k = 0; k < off; ++k) if (a.data[k] != b.data[k]) return false;
    const size_t n = (a.data.size() - off) / 8;
    for (size_t i = 0; i < n; ++i) {
        if (std::memcmp(&a.data[off + i * 8], &b.data[off + i * 8], 8) == 0) continue;
        double x, y;
        std::memcpy(&x, &a.data[off + i * 8], 8);
        std::memcpy(&y, &b.data[off + i * 8], 8);
        if (std::isnan(x) || std::isnan(y)) return false;
        if (!(std::fabs(x - y) <= 1e-6 * std::max(1.0, std::fabs(x)))) return false;
    }
    for (size_t i = off + n * 8; i < a.data.size(); ++i) if (a.data[i] != b.data[i]) return false;
    return true;
}

inline std::string proxy_difference(const std::vector<uint8_t>& autocad, const std::vector<uint8_t>& ours, bool skip_markers, bool skip_zero_markers = false) {
    std::vector<ProxyCommand> a = proxy_commands(autocad, skip_markers, skip_zero_markers), b = proxy_commands(ours, skip_markers, skip_zero_markers);
    for (size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
        if (i >= a.size() || i >= b.size()) return std::to_string(a.size()) + " commands against " + std::to_string(b.size());
        if (!command_equal(a[i], b[i])) {
            return "command " + std::to_string(i) + ": type " + std::to_string(a[i].type) + " (" + std::to_string(a[i].data.size()) + " bytes) against type " +
                   std::to_string(b[i].type) + " (" + std::to_string(b[i].data.size()) + " bytes), of " + std::to_string(a.size()) + " and " + std::to_string(b.size());
        }
    }
    return {};
}

// ---- the tables of a drawing

inline std::vector<Entity*> tables_of(Drawing& d) {
    std::vector<Entity*> out;
    for (Entity& e : d.entities()) if (e.type == EntityType::Table) out.push_back(&e);
    for (hdxf::Block& b : d.blocks()) for (Entity& e : b.entities) if (e.type == EntityType::Table) out.push_back(&e);
    return out;
}

} // namespace hdxf_table
