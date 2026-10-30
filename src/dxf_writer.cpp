#include "dxf_writer.hpp"
#include "text_encoding.hpp"
#include "handles.hpp"
#include "extents.hpp"
#include "header_defaults.hpp"
#include "dimension_block.hpp"
#include "text_layout.hpp"
#include <set>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace hdxf {
namespace internal {

bool names_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) !=
            std::toupper(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

bool is_truetype_style(const TextStyle& s) {
    std::string ext = utf8_from_path(path_from_utf8(s.font_file).extension());
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".ttf" || ext == ".ttc" || ext == ".otf" || ext == ".otc") return true;
    return s.font_file.empty() && !s.font_family.empty();
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

DxfWriter::DxfWriter(std::ostream& out, DxfVersion version, DxfFormat format)
    : out_(out), version_(version), format_(format),
      groups_(out, format, binary_code_width(version))
{
}

// ---------------------------------------------------------------------------
// Low-level output (group_io.hpp)
// ---------------------------------------------------------------------------

void DxfWriter::write_sentinel() {
    groups_.write_sentinel();
}

// The keywords that mark the structure are written with the code line width the file read had for them
// (DxfWriter::set_marker_columns()); "0 SECTION" waits for its name, which says which section it is
void DxfWriter::write_code(int code, const std::string& value) {
    if (!marker_columns_ || marker_columns_->empty() || format_ == DxfFormat::Binary) {
        groups_.write_string(code, value);
        return;
    }
    auto columns_of = [&](const std::string& key) {
        auto it = marker_columns_->find(key);
        return it == marker_columns_->end() ? 0 : it->second;
    };
    if (code == 0 && value == "SECTION") {
        section_open_ = true;
        return;
    }
    if (section_open_) {
        section_open_ = false;
        marker_section_ = code == 2 ? value : std::string();
        groups_.write_raw(0, "SECTION", false, columns_of("SECTION:" + marker_section_));
        if (code == 2) {
            groups_.write_raw(2, value, false, columns_of("NAME:" + value));
            return;
        }
    }
    int columns = 0;
    if (code == 0 && value == "ENDSEC") columns = columns_of("ENDSEC:" + marker_section_);
    else if (code == 0 && value == "ENDTAB") columns = columns_of("ENDTAB#" + std::to_string(endtabs_written_++));
    else if (code == 0 && value == "EOF") columns = columns_of("EOF");
    if (columns != 0) groups_.write_raw(code, value, false, columns);
    else groups_.write_string(code, value);
}

void DxfWriter::write_code_int(int code, int value) {
    groups_.write_int16(code, value);
}

void DxfWriter::write_code_double(int code, double value) {
    groups_.write_double(code, value);
}

void DxfWriter::write_code_int32(int code, int32_t value) {
    groups_.write_int32(code, value);
}

void DxfWriter::write_code_typed(int code, const std::string& value) {
    groups_.write_typed(code, value);
}
// ---------------------------------------------------------------------------
// Top-level write
// ---------------------------------------------------------------------------

// $DWGCODEPAGE of a drawing as a Windows code page number; `fallback` when it has none
int DxfWriter::header_code_page(const Drawing& dwg, int fallback) {
    const HeaderVar* code_page = dwg.find_header("$DWGCODEPAGE");
    if (!code_page || code_page->values.empty()) return fallback;
    return code_page_from_name(code_page->values[0].second);
}

bool DxfWriter::write(const Drawing& dwg) {
    if (version_ != DxfVersion::R12) return write_r2000(dwg);
    drawing_ = &dwg;

    // Strings are written in the drawing's code page (default ANSI_1252)
    int page = header_code_page(dwg, 1252);
    groups_.set_code_page(page);
    // The preserved strings are in the code page the file had; converted if $DWGCODEPAGE changed
    groups_.set_source_code_page(original_ ? header_code_page(*original_, 1252) : page);
    groups_.set_raw_code_page(page);

    // Anonymous blocks for DIMENSION entities that lack them — before the extents, which
    // count a dimension's block. Their entities and ENDBLK get handles, like all others.
    generate_dim_blocks(dwg);
    for (auto& b : generated_dim_blocks_) {
        b.endblk_handle = new_handle();
        for (auto& e : b.entities) e.props.handle = new_handle();
    }
    // The ENDBLK of $MODEL_SPACE and $PAPER_SPACE (written in BLOCKS when the drawing has no such
    // blocks): handles before $HANDSEED, which is above every handle in the file
    if (!r12_template()) {
        for (const char* name : {"$MODEL_SPACE", "$PAPER_SPACE"}) {
            bool present = false;
            for (const auto& block : dwg.blocks()) present = present || names_equal(block.name, name);
            if (!present) space_endblk_handles_[name] = new_handle();
        }
    }
    if (!error_.empty()) return false;
    compute_extents(dwg);
    if (r12_template()) prepare_r12_template(dwg);

    if (format_ == DxfFormat::Binary) {
        write_sentinel();
    }

    // R12 read from a file: HEADER, TABLES, BLOCKS and ENTITIES from the file's records
    bool has_header = false;
    for (const auto& s : dwg.raw_sections()) has_header = has_header || s.name == "HEADER";
    if (r12_template() && has_header) {
        write_header_template(dwg);
        write_tables(dwg);
        write_blocks(dwg.blocks());
        write_entities(dwg.entities());
        write_raw_sections_after_objects(dwg);
        write_eof();
        return out_.good() && groups_.error().empty() && error_.empty();
    }

    // Build header vars with updated extents
    std::vector<HeaderVar> header_vars = dwg.header_vars();
    auto set_header = [&](const std::string& name, std::vector<std::pair<int, std::string>> values) {
        for (auto& hv : header_vars) {
            if (hv.name == name) {
                hv.values = std::move(values);
                return;
            }
        }
        header_vars.push_back({name, std::move(values)});
    };
    auto point = [](const Point3& p) -> std::vector<std::pair<int, std::string>> {
        char bx[64], by[64], bz[64];
        std::snprintf(bx, sizeof(bx), "%.17g", p.x);
        std::snprintf(by, sizeof(by), "%.17g", p.y);
        std::snprintf(bz, sizeof(bz), "%.17g", p.z);
        return {{10, bx}, {20, by}, {30, bz}};
    };
    set_header("$EXTMIN", point(ext_min_));
    set_header("$EXTMAX", point(ext_max_));
    set_header("$PEXTMIN", point(pext_min_));
    set_header("$PEXTMAX", point(pext_max_));
    // As AutoCAD writes R12: handles on, $HANDSEED above every handle in the file. An R12 file read
    // without handles (and without a HEADER) keeps its records as read, without 5: handles off, as
    // AutoCAD discards a drawing with $HANDLING 1 whose records have none
    bool handles = !r12_template();
    for (const auto& e : dwg.entities()) handles = handles || !e.props.handle.empty();
    for (const auto& b : dwg.blocks()) handles = handles || !b.handle.empty();
    for (const auto& l : dwg.layers()) handles = handles || !l.handle.empty();
    if (handles) {
        set_header("$HANDLING", {{70, "1"}});
        set_header("$HANDSEED", {{5, format_handle(next_handle_)}});
    } else {
        set_header("$HANDLING", {{70, "0"}});
        header_vars.erase(std::remove_if(header_vars.begin(), header_vars.end(),
                                         [](const HeaderVar& v) { return v.name == "$HANDSEED"; }),
                          header_vars.end());
    }

    write_header(header_vars);
    write_tables(dwg);
    write_blocks(dwg.blocks());
    write_entities(dwg.entities());
    write_eof();

    return out_.good() && groups_.error().empty();
}

// $EXTMIN/$EXTMAX and $PEXTMIN/$PEXTMAX (extents.hpp). Called after the dimension
// blocks are generated and the changed ones regenerated (9.S, Codex 9): what is written counts.
void DxfWriter::compute_extents(const Drawing& dwg) {
    Extents model, paper;
    std::vector<std::string> font_warnings;
    drawing_extents(dwg, generated_dim_blocks_, dim_block_names_, model, paper, font_warnings, &regenerated_blocks_);
    if (r12_template()) {
        // Read from a file: the extents of the drawing as read are the file's (plan F9, F19). Only a
        // space whose geometry has changed gets new ones (write_header_template).
        Extents model_was, paper_was;
        std::vector<std::string> ignored;
        drawing_extents(*original_, {}, {}, model_was, paper_was, ignored);
        auto same = [](const Extents& a, const Extents& b) {
            if (a.empty() && b.empty()) return true;
            return a.min.x == b.min.x && a.min.y == b.min.y && a.min.z == b.min.z &&
                   a.max.x == b.max.x && a.max.y == b.max.y && a.max.z == b.max.z;
        };
        model_extents_changed_ = !same(model, model_was);
        paper_extents_changed_ = !same(paper, paper_was);
        if (!model_extents_changed_ && !paper_extents_changed_) font_warnings.clear();
    }
    for (auto& w : font_warnings) warnings_.push_back(std::move(w));
    ext_min_ = model.min;
    ext_max_ = model.max;
    pext_min_ = paper.min;
    pext_max_ = paper.max;
    const bool older = version_ == DxfVersion::R13 || version_ == DxfVersion::R14;
    if (older && model.empty()) {
        ext_min_ = {1e20, 1e20, 1e20};
        ext_max_ = {-1e20, -1e20, -1e20};
    }

    // An empty paper space is 0,0,0 as AutoCAD 2025 writes it; 1e20/-1e20 only when the
    // file the drawing was read from had those (an uninitialised paper space)
    if (paper.empty()) {
        auto header_is = [&](const char* name, double value) {
            const HeaderVar* hv = dwg.find_header(name);
            if (!hv || hv->values.size() != 3) return false;
            for (const auto& [code, text] : hv->values) {
                if (std::strtod(text.c_str(), nullptr) != value) return false;
            }
            return true;
        };
        // R13 and R14: AutoCAD writes an empty space's extents as 1e20/-1e20 (format profile D2)
        bool sentinel = older ||
                        (header_is("$PEXTMIN", 1e20) && header_is("$PEXTMAX", -1e20));
        pext_min_ = sentinel ? Point3{1e20, 1e20, 1e20} : Point3{0.0, 0.0, 0.0};
        pext_max_ = sentinel ? Point3{-1e20, -1e20, -1e20} : Point3{0.0, 0.0, 0.0};
    }

    // The default *Active view shows the extents; a drawing without any shows its limits,
    // as a new drawing in AutoCAD does
    view_min_ = ext_min_;
    view_max_ = ext_max_;
    if (model.empty()) {
        auto limit = [&](const char* name, Point3 fallback) {
            if (const HeaderVar* hv = dwg.find_header(name)) {
                for (const auto& [code, value] : hv->values) {
                    if (code == 10) fallback.x = std::strtod(value.c_str(), nullptr);
                    if (code == 20) fallback.y = std::strtod(value.c_str(), nullptr);
                }
            }
            return fallback;
        };
        view_min_ = limit("$LIMMIN", {0.0, 0.0, 0.0});
        view_max_ = limit("$LIMMAX", {12.0, 9.0, 0.0});
        return;
    }
    // A view with no height (or width) does not open in TrueView (a drawing whose extents
    // are one point, e.g. a single text, or a single horizontal line): a point gets the
    // limits' size around it, a flat box is made square around its middle
    double width = view_max_.x - view_min_.x;
    double height = view_max_.y - view_min_.y;
    if (width <= 0.0 && height <= 0.0) {
        Point3 lo = {0.0, 0.0, 0.0}, hi = {12.0, 9.0, 0.0};
        if (const HeaderVar* hv = dwg.find_header("$LIMMIN")) {
            for (const auto& [code, value] : hv->values) {
                if (code == 10) lo.x = std::strtod(value.c_str(), nullptr);
                if (code == 20) lo.y = std::strtod(value.c_str(), nullptr);
            }
        }
        if (const HeaderVar* hv = dwg.find_header("$LIMMAX")) {
            for (const auto& [code, value] : hv->values) {
                if (code == 10) hi.x = std::strtod(value.c_str(), nullptr);
                if (code == 20) hi.y = std::strtod(value.c_str(), nullptr);
            }
        }
        width = hi.x - lo.x > 0.0 ? hi.x - lo.x : 12.0;
        height = hi.y - lo.y > 0.0 ? hi.y - lo.y : 9.0;
    } else if (height <= 0.0) {
        height = width;
    } else if (width <= 0.0) {
        width = height;
    } else {
        return;
    }
    Point3 middle = {(view_min_.x + view_max_.x) / 2.0, (view_min_.y + view_max_.y) / 2.0, 0.0};
    view_min_ = {middle.x - width / 2.0, middle.y - height / 2.0, view_min_.z};
    view_max_ = {middle.x + width / 2.0, middle.y + height / 2.0, view_max_.z};
}

// ---------------------------------------------------------------------------
// HEADER section
// ---------------------------------------------------------------------------

void DxfWriter::write_header(const std::vector<HeaderVar>& header_vars) {
    write_code(0, "SECTION");
    write_code(2, "HEADER");

    // Variables only 2000+ has ($FINGERPRINTGUID, $CELWEIGHT, $SORTENTS …, and 2007's
    // $CMATERIAL, $INTERFEREOBJVS …, 2010's $DIMTXTDIRECTION in a drawing made as 2000 … 2010
    // and set back to R12) are not R12. Empty values are written, as AutoCAD writes them.
    std::set<std::string> in_r12;
    for (const auto& v : default_r12_headers()) in_r12.insert(v.name);
    std::set<std::string> r2000_only;
    for (const auto& v : default_r2013_headers()) {
        if (!in_r12.count(v.name)) r2000_only.insert(v.name);
    }

    for (const auto& hv : header_vars) {
        if (r2000_only.count(hv.name)) continue;
        write_code(9, hv.name);
        for (const auto& [code, value] : hv.values) {
            // Determine group code type: 10-59 doubles, 60-79 ints, 0-9 strings, etc.
            // For header vars the values are stored as strings already, so we detect
            // the group code range and use the appropriate write method.
            if ((code >= 10 && code <= 59) || (code >= 140 && code <= 147)) {
                // Double value
                double dval = 0.0;
                try { dval = std::stod(value); } catch (...) {}
                write_code_double(code, dval);
            } else if ((code >= 60 && code <= 79) || (code >= 170 && code <= 178) ||
                       (code >= 270 && code <= 289) || (code >= 370 && code <= 389) ||
                       (code >= 400 && code <= 409)) {
                // Integer value
                int ival = 0;
                try { ival = std::stoi(value); } catch (...) {}
                write_code_int(code, ival);
            } else {
                // String value (codes 0-9, etc.)
                write_code(code, value);
            }
        }
    }

    write_code(0, "ENDSEC");
}

// ---------------------------------------------------------------------------
// TABLES section
// ---------------------------------------------------------------------------

void DxfWriter::write_tables(const Drawing& dwg) {
    write_code(0, "SECTION");
    write_code(2, "TABLES");

    write_vport_table(dwg.vports());
    write_ltype_table(dwg.linetypes());
    write_layer_table(dwg.layers());
    write_style_table(dwg.text_styles());
    write_view_table(dwg.views());
    write_ucs_table(dwg.ucs_list());
    write_appid_table(dwg);
    write_dimstyle_table(dwg.dim_styles());

    write_code(0, "ENDSEC");
}

// ---------------------------------------------------------------------------
// VPORT table
// ---------------------------------------------------------------------------

void DxfWriter::write_vport_table(const std::vector<Vport>& vports) {
    if (!vports.empty() || table_in_file("VPORT")) {
        write_table_head_r12("VPORT", static_cast<int>(vports.size()));

        for (const auto& entry : vports) {
          write_entry_r12(entry, entry.raw, [&](const Vport& vp) {
            write_code(0, "VPORT");
            write_code(2, vp.name);
            write_code_int(70, vp.flags);
            write_code_double(10, vp.lower_left.x);
            write_code_double(20, vp.lower_left.y);
            write_code_double(11, vp.upper_right.x);
            write_code_double(21, vp.upper_right.y);
            write_code_double(12, vp.center.x);
            write_code_double(22, vp.center.y);
            write_code_double(13, vp.snap_base.x);
            write_code_double(23, vp.snap_base.y);
            write_code_double(14, vp.snap_spacing.x);
            write_code_double(24, vp.snap_spacing.y);
            write_code_double(15, vp.grid_spacing.x);
            write_code_double(25, vp.grid_spacing.y);
            write_code_double(16, vp.view_dir.x);
            write_code_double(26, vp.view_dir.y);
            write_code_double(36, vp.view_dir.z);
            write_code_double(17, vp.target.x);
            write_code_double(27, vp.target.y);
            write_code_double(37, vp.target.z);
            write_code_double(40, vp.view_height);
            write_code_double(41, vp.aspect_ratio);
            write_code_double(42, vp.lens_length);
            write_code_double(43, vp.front_clip);
            write_code_double(44, vp.back_clip);
            write_code_double(50, vp.snap_angle);
            write_code_double(51, vp.twist_angle);
            write_code_int(71, vp.view_mode);
            write_code_int(72, vp.circle_zoom);
            write_code_int(73, vp.fast_zoom);
            write_code_int(74, vp.ucsicon);
            write_code_int(75, vp.snap_on);
            write_code_int(76, vp.grid_on);
            write_code_int(77, vp.snap_style);
            write_code_int(78, vp.snap_isopair);
          });
        }

        write_code(0, "ENDTAB");
    } else {
        // Write default *Active vport based on the extents, or the limits of an empty drawing
        double cx = (view_min_.x + view_max_.x) / 2.0;
        double cy = (view_min_.y + view_max_.y) / 2.0;
        double width = view_max_.x - view_min_.x;
        double height = view_max_.y - view_min_.y;
        double view_height = height * 1.1;
        double view_width = width * 1.1;
        double aspect = (view_height > 0.0) ? (view_width / view_height) : 1.0;

        write_code(0, "TABLE");
        write_code(2, "VPORT");
        write_code_int(70, 1);

        write_code(0, "VPORT");
        write_code(2, "*ACTIVE");
        write_code_int(70, 0);
        write_code_double(10, 0.0);
        write_code_double(20, 0.0);
        write_code_double(11, 1.0);
        write_code_double(21, 1.0);
        write_code_double(12, cx);
        write_code_double(22, cy);
        write_code_double(13, 0.0);
        write_code_double(23, 0.0);
        write_code_double(14, 10.0);
        write_code_double(24, 10.0);
        write_code_double(15, 10.0);
        write_code_double(25, 10.0);
        write_code_double(16, 0.0);
        write_code_double(26, 0.0);
        write_code_double(36, 1.0);
        write_code_double(17, 0.0);
        write_code_double(27, 0.0);
        write_code_double(37, 0.0);
        write_code_double(40, view_height);
        write_code_double(41, aspect);
        write_code_double(42, 50.0);
        write_code_double(43, 0.0);
        write_code_double(44, 0.0);
        write_code_double(50, 0.0);
        write_code_double(51, 0.0);
        write_code_int(71, 0);
        write_code_int(72, 100);
        write_code_int(73, 1);
        write_code_int(74, 3);
        write_code_int(75, 0);
        write_code_int(76, 0);
        write_code_int(77, 0);
        write_code_int(78, 0);

        write_code(0, "ENDTAB");
    }
}

// ---------------------------------------------------------------------------
// LTYPE table
// ---------------------------------------------------------------------------

void DxfWriter::write_ltype_table(const std::vector<Linetype>& linetypes) {
    // R12 as AutoCAD writes it: CONTINUOUS only, unless the drawing has its own. BYBLOCK and
    // BYLAYER are no LTYPE records before R13 (AutoCAD 2025's R12 file has a count of 1).
    struct StandardLinetype { const char* name; const char* description; };
    const StandardLinetype standard[] = {
        {"CONTINUOUS", "Solid line"},
    };

    // A drawing read from a file has the standard ones if the file has
    bool defaults = !r12_template();
    int count = static_cast<int>(linetypes.size());
    for (const auto& st : standard) {
        if (defaults && !has_entry_named(linetypes, st.name)) count++;
    }

    if (!table_wanted("LTYPE", linetypes.empty())) return;
    write_table_head_r12("LTYPE", count);

    for (const auto& st : standard) {
        if (!defaults || has_entry_named(linetypes, st.name)) continue;
        write_code(0, "LTYPE");
        write_code(2, st.name);
        write_code_int(70, 0);
        write_code(3, st.description);
        write_code_int(72, 65);
        write_code_int(73, 0);
        write_code_double(40, 0.0);
    }

    // User linetypes
    for (const auto& entry : linetypes) {
        write_entry_r12(entry, entry.raw, [&](const Linetype& lt) {
            write_code(0, "LTYPE");
            write_code(2, lt.name);
            write_code_int(70, lt.flags);
            write_code(3, lt.description);
            write_code_int(72, lt.alignment);
            write_code_int(73, static_cast<int>(lt.pattern.size()));
            write_code_double(40, lt.total_length);
            for (double dash : lt.pattern) {
                write_code_double(49, dash);
            }
        });
    }

    write_code(0, "ENDTAB");
}

// ---------------------------------------------------------------------------
// LAYER table
// ---------------------------------------------------------------------------

void DxfWriter::write_layer_table(const std::vector<Layer>& layers) {
    bool has_layer_0 = false;
    for (const auto& l : layers) {
        if (l.name == "0") { has_layer_0 = true; break; }
    }

    // A drawing read from a file has layer 0 if the file has
    bool defaults = !r12_template();
    int layer_count = static_cast<int>(layers.size()) + (has_layer_0 || !defaults ? 0 : 1) + (needs_defpoints_ ? 1 : 0);

    if (!table_wanted("LAYER", layers.empty() && !needs_defpoints_)) return;
    write_table_head_r12("LAYER", layer_count);

    // Default layer 0 only if user didn't provide one
    if (!has_layer_0 && defaults) {
        write_code(0, "LAYER");
        write_code(2, "0");
        write_code_int(70, 0);
        write_code_int(62, 7);
        write_code(6, "CONTINUOUS");
    }

    // Defpoints, for the dimension blocks built here (AutoCAD's: color 7, CONTINUOUS)
    if (needs_defpoints_) {
        write_code(0, "LAYER");
        write_code(2, "Defpoints");
        write_code_int(70, 0);
        write_code_int(62, 7);
        write_code(6, "CONTINUOUS");
    }

    // User layers (including "0" if provided)
    for (const auto& entry : layers) {
        write_entry_r12(entry, entry.raw, [&](const Layer& l) {
            write_code(0, "LAYER");
            write_code(2, l.name);

            int flags = 0;
            if (l.frozen) flags |= 1;
            if (l.frozen_in_new_vp) flags |= 2;
            if (l.locked) flags |= 4;
            write_code_int(70, flags);

            int color = l.color.aci;
            if (!l.on) color = -color;
            write_code_int(62, color);

            write_code(6, l.linetype);
        });
    }

    write_code(0, "ENDTAB");
}

// ---------------------------------------------------------------------------
// STYLE table
// ---------------------------------------------------------------------------

void DxfWriter::write_style_table(const std::vector<TextStyle>& styles) {
    bool has_standard = has_entry_named(styles, "Standard") || r12_template();
    int count = static_cast<int>(styles.size()) + (has_standard ? 0 : 1);

    if (!table_wanted("STYLE", styles.empty())) return;
    write_table_head_r12("STYLE", count);

    // Default Standard style, unless the drawing has its own
    if (!has_standard) {
        write_code(0, "STYLE");
        write_code(2, "Standard");
        write_code_int(70, 0);
        write_code_double(40, 0.0);
        write_code_double(41, 1.0);
        write_code_double(50, 0.0);
        write_code_int(71, 0);
        write_code_double(42, 2.5);
        write_code(3, "txt");
        write_code(4, "");
    }

    // User styles. R12 has no TrueType fonts: AutoCAD writes such a style with "txt"
    // (rättningsfas R.8, ACadSharp's sample_AC1009)
    for (const auto& entry : styles) {
        write_entry_r12(entry, entry.raw, [&](const TextStyle& s) {
            std::string font = s.font_file;
            if (is_truetype_style(s)) {
                font = "txt";
                warnings_.push_back("Text style \"" + s.name + "\": TrueType font written as txt (R12)");
            }
            write_code(0, "STYLE");
            write_code(2, s.name);
            write_code_int(70, s.flags);
            write_code_double(40, s.fixed_height);
            write_code_double(41, s.width_factor);
            write_code_double(50, s.oblique_angle);
            write_code_int(71, s.generation_flags);
            write_code_double(42, s.last_height);
            write_code(3, font);
            write_code(4, s.big_font);
        });
    }

    write_code(0, "ENDTAB");
}

// ---------------------------------------------------------------------------
// VIEW table
// ---------------------------------------------------------------------------

void DxfWriter::write_view_table(const std::vector<View>& views) {
    if (!table_wanted("VIEW", false)) return;
    write_table_head_r12("VIEW", static_cast<int>(views.size()));

    for (const auto& entry : views) {
        write_entry_r12(entry, entry.raw, [&](const View& v) {
            // The width after the center, as AutoCAD 2025 writes R12 (plan F18)
            write_code(0, "VIEW");
            write_code(2, v.name);
            write_code_int(70, v.flags);
            write_code_double(40, v.height);
            write_code_double(10, v.center.x);
            write_code_double(20, v.center.y);
            write_code_double(41, v.width);
            write_code_double(11, v.direction.x);
            write_code_double(21, v.direction.y);
            write_code_double(31, v.direction.z);
            write_code_double(12, v.target.x);
            write_code_double(22, v.target.y);
            write_code_double(32, v.target.z);
            write_code_double(42, v.lens_length);
            write_code_double(43, v.front_clip);
            write_code_double(44, v.back_clip);
            write_code_double(50, v.twist);
            write_code_int(71, v.view_mode);
        });
    }

    write_code(0, "ENDTAB");
}

// ---------------------------------------------------------------------------
// UCS table
// ---------------------------------------------------------------------------

void DxfWriter::write_ucs_table(const std::vector<Ucs>& ucs_list) {
    if (!table_wanted("UCS", false)) return;
    write_table_head_r12("UCS", static_cast<int>(ucs_list.size()));

    for (const auto& entry : ucs_list) {
        write_entry_r12(entry, entry.raw, [&](const Ucs& u) {
            write_code(0, "UCS");
            write_code(2, u.name);
            write_code_int(70, u.flags);
            write_code_double(10, u.origin.x);
            write_code_double(20, u.origin.y);
            write_code_double(30, u.origin.z);
            write_code_double(11, u.x_dir.x);
            write_code_double(21, u.x_dir.y);
            write_code_double(31, u.x_dir.z);
            write_code_double(12, u.y_dir.x);
            write_code_double(22, u.y_dir.y);
            write_code_double(32, u.y_dir.z);
        });
    }

    write_code(0, "ENDTAB");
}

// ---------------------------------------------------------------------------
// APPID table
// ---------------------------------------------------------------------------

std::vector<std::string> DxfWriter::missing_app_ids(const Drawing& dwg) const {
    std::vector<std::string> out;
    auto note = [&](const std::vector<XdataList>& xdata) {
        for (const auto& x : xdata) {
            if (names_equal(x.app_name, "ACAD") || has_entry_named(dwg.app_ids(), x.app_name)) continue;
            bool listed = std::any_of(out.begin(), out.end(), [&](const std::string& n) { return names_equal(n, x.app_name); });
            if (!listed) out.push_back(x.app_name);
        }
    };
    // Every record with XDATA: the tables (VPORT and LTYPE from 2000), BLOCK_RECORD and BLOCK,
    // the entities and the objects
    for (const auto& v : dwg.vports()) note(v.xdata);
    for (const auto& lt : dwg.linetypes()) note(lt.xdata);
    for (const auto& l : dwg.layers()) note(l.xdata);
    for (const auto& s : dwg.text_styles()) note(s.xdata);
    for (const auto& d : dwg.dim_styles()) note(d.xdata);
    // A locked attribute's XDATA AcDbAttr, which the writer makes up to 2004 (B33); the
    // version of a MULTILEADER or MLEADERSTYLE up to 2007 (B36)
    const std::vector<XdataList> lock = {{"AcDbAttr", {}}};
    const std::vector<XdataList> mleader_version = {{"ACAD_MLEADERVER", {}}};
    bool lock_xdata = version_ < DxfVersion::R2007;
    auto entities = [&](const std::vector<Entity>& list) {
        for (const auto& e : list) {
            note(e.props.xdata);
            if (e.type == EntityType::Polyline) {
                for (const auto& v : e.as_polyline().vertices) note(v.xdata);       // a vertex's own (F16)
            }
            if (e.type == EntityType::Insert) {
                for (const auto& a : e.as_insert().attributes) {
                    note(a.props.xdata);
                    if (lock_xdata && a.data.lock_position) note(lock);
                }
            }
            if (lock_xdata && e.type == EntityType::Attdef && e.as_attdef().lock_position) note(lock);
            if (lock_xdata && e.type == EntityType::Attrib && e.as_attrib().lock_position) note(lock);
            if (e.type == EntityType::MLeader && version_ < DxfVersion::R2010) note(mleader_version);
        }
    };
    entities(dwg.entities());
    for (const auto& b : dwg.blocks()) {
        note(b.record.xdata);
        note(b.xdata);
        entities(b.entities);
    }
    for (const auto& b : generated_dim_blocks_) entities(b.entities);
    for (const auto& o : dwg.objects()) {
        note(o.xdata);
        if (o.type == ObjectType::MLeaderStyle && version_ < DxfVersion::R2010) note(mleader_version);
    }
    return out;
}

void DxfWriter::write_appid_table(const Drawing& dwg) {
    const std::vector<AppId>& app_ids = dwg.app_ids();
    bool has_acad = has_entry_named(app_ids, "ACAD") || r12_template();
    std::vector<std::string> missing = missing_app_ids(dwg);
    int count = static_cast<int>(app_ids.size() + missing.size()) + (has_acad ? 0 : 1);

    if (!table_wanted("APPID", app_ids.empty() && missing.empty())) return;
    write_table_head_r12("APPID", count);

    // ACAD first, unless the drawing has its own
    if (!has_acad) {
        write_code(0, "APPID");
        write_code(2, "ACAD");
        write_code_int(70, 0);
    }

    // User app IDs, then those the drawing's XDATA needs
    for (const auto& entry : app_ids) {
        write_entry_r12(entry, entry.raw, [&](const AppId& a) {
            write_code(0, "APPID");
            write_code(2, a.name);
            write_code_int(70, a.flags);
        });
    }
    for (const auto& name : missing) {
        write_code(0, "APPID");
        write_code(2, name);
        write_code_int(70, 0);
    }

    write_code(0, "ENDTAB");
}

// ---------------------------------------------------------------------------
// DIMSTYLE table
// R12: code 5 = DIMBLK (block name), NOT a handle
// ---------------------------------------------------------------------------

void DxfWriter::write_dimstyle_table(const std::vector<DimStyle>& dimstyles) {
    bool has_standard = has_entry_named(dimstyles, "Standard") || r12_template();
    int count = static_cast<int>(dimstyles.size()) + (has_standard ? 0 : 1);

    if (!table_wanted("DIMSTYLE", dimstyles.empty())) return;
    write_table_head_r12("DIMSTYLE", count);

    // Default "Standard" DimStyle, unless the drawing has its own
    if (!has_standard) write_default_dimstyle();

    // User dimstyles. R12 has neither the variables of 2000+ nor a style's XDATA; AutoCAD's
    // R12 file leaves them out (ACadSharp's sample_AC1009), a warning says so here.
    const DimStyle d0 = autocad_dim_style();
    for (const auto& entry : dimstyles) {
      write_entry_r12(entry, entry.raw, [&](const DimStyle& ds) {
        bool newer = ds.dimtfill != d0.dimtfill || ds.dimtfillclr.aci != d0.dimtfillclr.aci ||
                     ds.dimtfillclr.rgb != d0.dimtfillclr.rgb || !ds.dimltype.empty() ||
                     !ds.dimltex1.empty() || !ds.dimltex2.empty() || ds.dimfxlon != d0.dimfxlon ||
                     ds.dimfxl != d0.dimfxl || ds.dimjogang != d0.dimjogang || !ds.xdata.empty();
        if (newer) {
            warnings_.push_back("Dimension style \"" + ds.name +
                                "\": text fill, linetypes, fixed extension lines, jog angle and XDATA not written (R12)");
        }
        write_code(0, "DIMSTYLE");
        write_code(2, ds.name);
        write_code_int(70, ds.flags);

        // String variables (3-7)
        write_code(3, ds.dimpost);
        write_code(4, ds.dimapost);
        write_code(5, ds.dimblk);      // R12: block name, NOT handle
        write_code(6, ds.dimblk1);
        write_code(7, ds.dimblk2);

        // Double variables (40-48)
        write_code_double(40, ds.dimscale);
        write_code_double(41, ds.dimasz);
        write_code_double(42, ds.dimexo);
        write_code_double(43, ds.dimdli);
        write_code_double(44, ds.dimexe);
        write_code_double(45, ds.dimrnd);
        write_code_double(46, ds.dimdle);
        write_code_double(47, ds.dimtp);
        write_code_double(48, ds.dimtm);

        // Double variables (140-147)
        write_code_double(140, ds.dimtxt);
        write_code_double(141, ds.dimcen);
        write_code_double(142, ds.dimtsz);
        write_code_double(143, ds.dimaltf);
        write_code_double(144, ds.dimlfac);
        write_code_double(145, ds.dimtvp);
        write_code_double(146, ds.dimtfac);
        write_code_double(147, ds.dimgap);

        // Int variables (71-78)
        write_code_int(71, ds.dimtol);
        write_code_int(72, ds.dimlim);
        write_code_int(73, ds.dimtih);
        write_code_int(74, ds.dimtoh);
        write_code_int(75, ds.dimse1);
        write_code_int(76, ds.dimse2);
        write_code_int(77, ds.dimtad);
        write_code_int(78, ds.dimzin);

        // Int variables (170-178)
        write_code_int(170, ds.dimalt);
        write_code_int(171, ds.dimaltd);
        write_code_int(172, ds.dimtofl);
        write_code_int(173, ds.dimsah);
        write_code_int(174, ds.dimtix);
        write_code_int(175, ds.dimsoxd);
        write_code_int(176, ds.dimclrd);
        write_code_int(177, ds.dimclre);
        write_code_int(178, ds.dimclrt);
      });
    }

    write_code(0, "ENDTAB");
}

void DxfWriter::write_default_dimstyle() {
    write_code(0, "DIMSTYLE");
    write_code(2, "Standard");
    write_code_int(70, 0);
    write_code(3, "");       // dimpost
    write_code(4, "");       // dimapost
    write_code(5, "");       // dimblk
    write_code(6, "");       // dimblk1
    write_code(7, "");       // dimblk2
    write_code_double(40, 1.0);    // dimscale
    write_code_double(41, 2.5);    // dimasz
    write_code_double(42, 0.625);  // dimexo
    write_code_double(43, 3.75);   // dimdli
    write_code_double(44, 0.625);  // dimexe
    write_code_double(45, 0.0);    // dimrnd
    write_code_double(46, 0.0);    // dimdle
    write_code_double(47, 0.0);    // dimtp
    write_code_double(48, 0.0);    // dimtm
    write_code_double(140, 2.5);   // dimtxt
    write_code_double(141, 2.5);   // dimcen
    write_code_double(142, 0.0);   // dimtsz
    write_code_double(143, 25.4);  // dimaltf
    write_code_double(144, 1.0);   // dimlfac
    write_code_double(145, 0.0);   // dimtvp
    write_code_double(146, 1.0);   // dimtfac
    write_code_double(147, 0.625); // dimgap
    write_code_int(71, 0);   // dimtol
    write_code_int(72, 0);   // dimlim
    write_code_int(73, 0);   // dimtih
    write_code_int(74, 0);   // dimtoh
    write_code_int(75, 0);   // dimse1
    write_code_int(76, 0);   // dimse2
    write_code_int(77, 0);   // dimtad
    write_code_int(78, 0);   // dimzin
    write_code_int(170, 0);  // dimalt
    write_code_int(171, 2);  // dimaltd
    write_code_int(172, 0);  // dimtofl
    write_code_int(173, 0);  // dimsah
    write_code_int(174, 0);  // dimtix
    write_code_int(175, 0);  // dimsoxd
    write_code_int(176, 0);  // dimclrd
    write_code_int(177, 0);  // dimclre
    write_code_int(178, 0);  // dimclrt
}

// ---------------------------------------------------------------------------
// BLOCKS section
// ---------------------------------------------------------------------------

void DxfWriter::write_blocks(const std::vector<Block>& blocks) {
    write_code(0, "SECTION");
    write_code(2, "BLOCKS");

    // R12 as AutoCAD writes it: $MODEL_SPACE and $PAPER_SPACE first, empty (the entities are in
    // ENTITIES), the paper space with 67 1 on BLOCK and ENDBLK. A drawing that has them (read
    // from R12) writes its own below.
    for (const char* name : {"$MODEL_SPACE", "$PAPER_SPACE"}) {
        auto reserved = space_endblk_handles_.find(name);
        // (a drawing read from a file has them as the file has them)
        if (reserved == space_endblk_handles_.end()) continue;
        bool paper = std::string(name) == "$PAPER_SPACE";
        write_code(0, "BLOCK");
        if (paper) write_code_int(67, 1);
        write_code(8, "0");
        write_code(2, name);
        write_code_int(70, 0);
        write_code_double(10, 0.0);
        write_code_double(20, 0.0);
        write_code_double(30, 0.0);
        write_code(3, name);
        write_code(1, "");
        write_code(0, "ENDBLK");
        write_code(5, reserved->second);
        if (paper) write_code_int(67, 1);
        write_code(8, "0");
    }

    // Write generated dimension pseudo-blocks first (anonymous *D blocks)
    for (const auto& block : generated_dim_blocks_) {
        // 8, 2, 70, 10, 3, 1 — as AutoCAD writes BLOCK in R12 (ACadSharp's sample_AC1009)
        write_code(0, "BLOCK");
        write_code(8, block.layer);
        if (block.block_color) write_code_int(62, block.block_color->aci);
        write_code(2, block.name);
        write_code_int(70, block.flags);
        write_code_double(10, block.base_point.x);
        write_code_double(20, block.base_point.y);
        write_code_double(30, block.base_point.z);
        write_code(3, block.name);
        write_code(1, "");

        for (const auto& entity : block.entities) {
            write_entity_r12(entity);
        }

        write_code(0, "ENDBLK");
        write_code(5, block.endblk_handle);
        write_code(8, block.layer);
        if (block.endblk_color) write_code_int(62, block.endblk_color->aci);
    }

    // Write user-defined blocks. *Model_Space and *Paper_Space of a DXF 2000 drawing (they
    // have a BLOCK_RECORD, see set_version()) are not blocks in R12.
    for (const auto& block : blocks) {
        if (!block.record.handle.empty() &&
            (is_model_space(block.name) || is_paper_space(block.name))) continue;

        write_entry_r12(block, block.raw, [&](const Block& b) { write_block_begin_r12(b); });

        // Write block entities
        for (const auto& entity : block.entities) {
            write_entity_r12(entity);
        }

        write_entry_r12(block, block.endblk_raw, [&](const Block& b) { write_block_end_r12(b); });
    }

    write_code(0, "ENDSEC");
}

// BLOCK as AutoCAD writes it in R12: 67 (paper space), 8, 2, 70, 10, 3, 1
void DxfWriter::write_block_begin_r12(const Block& block) {
    write_code(0, "BLOCK");
    if (block.paper_space != 0) write_code_int(67, block.paper_space);
    write_code(8, block.layer);
    // The BLOCK's own color (AutoCAD's R12 file: 8, 62, 2), none for ByLayer
    if (block.block_color && !block.block_color->is_by_layer()) write_code_int(62, block.block_color->aci);
    write_code(2, block.name);

    // Auto-set flag bit 2 if block contains ATTDEFs
    int flags = block.flags;
    for (const auto& entity : block.entities) {
        if (entity.type == EntityType::Attdef) {
            flags |= 2;
            break;
        }
    }
    write_code_int(70, flags);

    write_code_double(10, block.base_point.x);
    write_code_double(20, block.base_point.y);
    write_code_double(30, block.base_point.z);
    write_code(3, block.name);
    write_code(1, block.xref_path);     // always, empty when no xref (AutoCAD's R12 file)
}

void DxfWriter::write_block_end_r12(const Block& block) {
    write_code(0, "ENDBLK");
    if (!block.endblk_handle.empty()) write_code(5, block.endblk_handle);
    if (block.paper_space != 0) write_code_int(67, block.paper_space);
    write_code(8, block.layer);
    // The ENDBLK's own color (AutoCAD's R12 file: 5, 8, 62), none for ByLayer
    if (block.endblk_color && !block.endblk_color->is_by_layer()) write_code_int(62, block.endblk_color->aci);
}

// ---------------------------------------------------------------------------
// Common entity codes
// ---------------------------------------------------------------------------

// What an entity has that R12 cannot write: its linetype scale (48) and visibility (60), which
// the R12 reference does not have — a warning, once per kind
void DxfWriter::note_r12_only(const EntityProperties& p) {
    if (p.linetype_scale != 1.0) note_once("Linetype scale per entity (48) not written (R12)");
    if (!p.visible) note_once("Invisible entities (60) written visible (R12)");
}

// A warning, once
void DxfWriter::note_once(const std::string& what) {
    if (std::find(warnings_.begin(), warnings_.end(), what) == warnings_.end()) warnings_.push_back(what);
}

// The handle comes right after 0, as AutoCAD writes R12. The extrusion (210, 220, 230) follows
// the geometry in LINE, CIRCLE, ARC and SOLID (AutoCAD 2025, plan F11): those write it
// themselves with write_extrusion_r12().
void DxfWriter::write_common_entity_codes(const Entity& entity, bool with_extrusion) {
    if (!entity.props.handle.empty()) write_code(5, entity.props.handle);
    // 67 before the layer, as AutoCAD writes it (its R12 file: 5, 67, 8 on BLOCK and ENDBLK)
    if (entity.props.paper_space != 0) {
        write_code_int(67, entity.props.paper_space);
    }
    write_code(8, entity.props.layer);

    if (!entity.props.linetype.empty()) {
        write_code(6, entity.props.linetype);
    }

    if (!entity.props.color.is_by_layer()) {
        write_code_int(62, entity.props.color.aci);
    }

    // R12 has no linetype scale (48) or visibility (60) per entity (the R12 reference's
    // table 15); a warning says what is lost
    note_r12_only(entity.props);

    if (entity.props.thickness != 0.0) {
        write_code_double(39, entity.props.thickness);
    }

    if (with_extrusion) write_extrusion_r12(entity);
}

// Extrusion direction (if not default 0,0,1)
void DxfWriter::write_extrusion_r12(const Entity& entity) {
    if (entity.props.extrusion.x != 0.0 ||
        entity.props.extrusion.y != 0.0 ||
        entity.props.extrusion.z != 1.0) {
        write_code_double(210, entity.props.extrusion.x);
        write_code_double(220, entity.props.extrusion.y);
        write_code_double(230, entity.props.extrusion.z);
    }
}

// ---------------------------------------------------------------------------
// XDATA
// ---------------------------------------------------------------------------

void DxfWriter::write_xdata(const std::vector<XdataList>& xdata) {
    for (const auto& xd : xdata) {
        write_code(1001, xd.app_name);
        for (const auto& item : xd.items) {
            write_code_typed(item.code, item.value);
        }
    }
}

// As AutoCAD writes a locked attribute in R12, 2000 and 2004 (ACadSharp's sample_AC1009 …
// AC1018): after the attribute's other XDATA; 2007 has 280 in the record instead
void DxfWriter::write_attribute_lock(bool locked) {
    if (!locked || version_ >= DxfVersion::R2007) return;
    write_code(1001, "AcDbAttr");
    groups_.write_int(1070, 0);
    groups_.write_int(1070, 1);
}

// ---------------------------------------------------------------------------
// Entity dispatch
// ---------------------------------------------------------------------------

void DxfWriter::write_single_entity(const Entity& entity) {
    switch (entity.type) {
        case EntityType::Line:      write_line(entity); break;
        case EntityType::Circle:    write_circle(entity); break;
        case EntityType::Arc:       write_arc(entity); break;
        case EntityType::Point:     write_point_entity(entity); break;
        case EntityType::Text:      write_text(entity); break;
        case EntityType::Polyline:  write_polyline(entity); break;
        case EntityType::Trace:     write_trace(entity); break;
        case EntityType::Solid:     write_solid(entity); break;
        case EntityType::Face3D:    write_face3d(entity); break;
        case EntityType::Shape:     write_shape(entity); break;
        case EntityType::Insert:    write_insert(entity); break;
        case EntityType::Attdef:    write_attdef(entity); break;
        case EntityType::Attrib:    write_attrib_entity(entity); break;
        case EntityType::Dimension: write_dimension(entity); break;
        case EntityType::Viewport:  write_viewport_entity(entity); break;
        // DXF 2000 types: write_dxf() refuses them for R12 (B11)
        case EntityType::LwPolyline:
        case EntityType::Ellipse:
        case EntityType::Xline:
        case EntityType::Ray:
        case EntityType::MText:
        case EntityType::Tolerance:
        case EntityType::Leader:
        case EntityType::Spline:
        case EntityType::Hatch:
        case EntityType::Mline:
        case EntityType::Image:
        case EntityType::Wipeout:
        case EntityType::Light:
        case EntityType::SectionObject:
        case EntityType::ArcDimension:              // 2000 and later (step 9.17): the codec's
        case EntityType::LargeRadialDimension:
        case EntityType::CoordinationModel:
        case EntityType::PdfUnderlay:
        case EntityType::DwfUnderlay:
        case EntityType::DgnUnderlay:
        case EntityType::Solid3D:
        case EntityType::Region:
        case EntityType::Body:
        case EntityType::MLeader:
        case EntityType::Surface:
        case EntityType::ExtrudedSurface:
        case EntityType::LoftedSurface:
        case EntityType::RevolvedSurface:
        case EntityType::SweptSurface:
        case EntityType::PlaneSurface:
        case EntityType::NurbsSurface:
        case EntityType::Helix:
        case EntityType::OleFrame:
        case EntityType::Ole2Frame:
        case EntityType::ProxyEntity:
        case EntityType::Table:
        case EntityType::TcPoint:
        case EntityType::Mesh:      break;
        case EntityType::Unknown:   write_unknown(entity); break;
    }
}

// ---------------------------------------------------------------------------
// LINE
// ---------------------------------------------------------------------------

void DxfWriter::write_line(const Entity& entity) {
    const auto& d = entity.as_line();
    write_code(0, "LINE");
    write_common_entity_codes(entity, false);
    write_code_double(10, d.start.x);
    write_code_double(20, d.start.y);
    write_code_double(30, d.start.z);
    write_code_double(11, d.end.x);
    write_code_double(21, d.end.y);
    write_code_double(31, d.end.z);
    write_extrusion_r12(entity);
    write_xdata(entity.props.xdata);
}

// ---------------------------------------------------------------------------
// CIRCLE
// ---------------------------------------------------------------------------

void DxfWriter::write_circle(const Entity& entity) {
    const auto& d = entity.as_circle();
    write_code(0, "CIRCLE");
    write_common_entity_codes(entity, false);
    write_code_double(10, d.center.x);
    write_code_double(20, d.center.y);
    write_code_double(30, d.center.z);
    write_code_double(40, d.radius);
    write_extrusion_r12(entity);
    write_xdata(entity.props.xdata);
}

// ---------------------------------------------------------------------------
// ARC
// ---------------------------------------------------------------------------

void DxfWriter::write_arc(const Entity& entity) {
    const auto& d = entity.as_arc();
    write_code(0, "ARC");
    write_common_entity_codes(entity, false);
    write_code_double(10, d.center.x);
    write_code_double(20, d.center.y);
    write_code_double(30, d.center.z);
    write_code_double(40, d.radius);
    write_extrusion_r12(entity);
    write_code_double(50, d.start_angle);
    write_code_double(51, d.end_angle);
    write_xdata(entity.props.xdata);
}

// ---------------------------------------------------------------------------
// POINT
// ---------------------------------------------------------------------------

void DxfWriter::write_point_entity(const Entity& entity) {
    const auto& d = entity.as_point();
    write_code(0, "POINT");
    write_common_entity_codes(entity);
    write_code_double(10, d.location.x);
    write_code_double(20, d.location.y);
    write_code_double(30, d.location.z);
    if (d.x_axis_angle != 0.0) {
        write_code_double(50, d.x_axis_angle);
    }
    write_xdata(entity.props.xdata);
}

// ---------------------------------------------------------------------------
// TEXT
// ---------------------------------------------------------------------------

void DxfWriter::write_text(const Entity& entity) {
    const auto& d = entity.as_text();
    write_code(0, "TEXT");
    write_common_entity_codes(entity);
    write_code_double(10, d.position.x);
    write_code_double(20, d.position.y);
    write_code_double(30, d.position.z);
    write_code_double(40, d.height);
    write_code(1, d.content);

    if (d.rotation != 0.0) {
        write_code_double(50, d.rotation);
    }

    if (d.width_factor != 1.0) {
        write_code_double(41, d.width_factor);
    }

    if (d.oblique_angle != 0.0) {
        write_code_double(51, d.oblique_angle);
    }

    if (d.style != "Standard") {
        write_code(7, d.style);
    }

    if (d.generation_flags != 0) {
        write_code_int(71, d.generation_flags);
    }

    // 72, the alignment point, then 73 — as AutoCAD writes TEXT (in 2000+: 72 and 11 in the first
    // AcDbText, 73 in the second)
    if (d.h_justify != 0) {
        write_code_int(72, d.h_justify);
    }

    if (d.h_justify != 0 || d.v_justify != 0) {
        write_code_double(11, d.alignment_point.x);
        write_code_double(21, d.alignment_point.y);
        write_code_double(31, d.alignment_point.z);
    }

    if (d.v_justify != 0) {
        write_code_int(73, d.v_justify);
    }

    write_xdata(entity.props.xdata);
}

// ---------------------------------------------------------------------------
// POLYLINE + VERTEX + SEQEND (R12 heavy polyline format)
// ---------------------------------------------------------------------------

void DxfWriter::write_polyline(const Entity& entity) {
    write_polyline_head(entity);
    for (const auto& v : entity.as_polyline().vertices) write_vertex(entity, v);
    write_polyline_seqend(entity);
}

// POLYLINE header
void DxfWriter::write_polyline_head(const Entity& entity) {
    const auto& d = entity.as_polyline();
    write_code(0, "POLYLINE");
    write_common_entity_codes(entity);

    write_code_int(66, 1);  // vertices follow flag
    write_code_double(10, d.header_x);      // 0 in a 2D polyline; a point of the polyline when AutoCAD
    write_code_double(20, d.header_y);      //     makes a 3D one of another type
    write_code_double(30, d.elevation);     // the elevation (the R12 reference)

    // 70 is optional with default 0 (the R12 reference)
    if (d.flags != 0) write_code_int(70, d.flags);

    if (d.default_start_width != 0.0) {
        write_code_double(40, d.default_start_width);
    }
    if (d.default_end_width != 0.0) {
        write_code_double(41, d.default_end_width);
    }
    if (d.m_count != 0) {
        write_code_int(71, d.m_count);
    }
    if (d.n_count != 0) {
        write_code_int(72, d.n_count);
    }
    if (d.smooth_m != 0) {
        write_code_int(73, d.smooth_m);
    }
    if (d.smooth_n != 0) {
        write_code_int(74, d.smooth_n);
    }
    if (d.smooth_type != 0) {
        write_code_int(75, d.smooth_type);
    }
    // XDATA belongs to the POLYLINE record (it used to follow SEQEND, and was lost on
    // reading)
    write_xdata(entity.props.xdata);
}

// A VERTEX has the layer of its polyline unless it has one of its own, and its own linetype and
// color when it has them (BYBLOCK as AutoCAD writes it, 62 = 0), plan F15
void DxfWriter::write_vertex(const Entity& polyline, const Vertex& v) {
    write_code(0, "VERTEX");
    if (!v.handle.empty()) write_code(5, v.handle);
    if (polyline.props.paper_space != 0) write_code_int(67, polyline.props.paper_space);
    write_code(8, v.layer.empty() ? polyline.props.layer : v.layer);
    if (!v.linetype.empty()) write_code(6, v.linetype);
    if (!v.color.is_by_layer()) write_code_int(62, v.color.aci);
    write_code_double(10, v.position.x);
    write_code_double(20, v.position.y);
    write_code_double(30, v.position.z);

    if (v.start_width != 0.0) {
        write_code_double(40, v.start_width);
    }
    if (v.end_width != 0.0) {
        write_code_double(41, v.end_width);
    }
    if (v.bulge != 0.0) {
        write_code_double(42, v.bulge);
    }
    if (v.flags != 0) {
        write_code_int(70, v.flags);
    }
    if (v.tangent != 0.0) {
        write_code_double(50, v.tangent);
    }
    // Polyface mesh vertex indices (groups 71-74)
    if (v.face[0] != 0) write_code_int(71, v.face[0]);
    if (v.face[1] != 0) write_code_int(72, v.face[1]);
    if (v.face[2] != 0) write_code_int(73, v.face[2]);
    if (v.face[3] != 0) write_code_int(74, v.face[3]);
    write_xdata(v.xdata);                   // the vertex's own (plan F16)
}

void DxfWriter::write_polyline_seqend(const Entity& polyline) {
    const auto& d = polyline.as_polyline();
    write_code(0, "SEQEND");
    if (!d.seqend_handle.empty()) write_code(5, d.seqend_handle);
    if (polyline.props.paper_space != 0) write_code_int(67, polyline.props.paper_space);
    write_code(8, d.seqend_layer.empty() ? polyline.props.layer : d.seqend_layer);
    if (!d.seqend_linetype.empty()) write_code(6, d.seqend_linetype);
    if (!d.seqend_color.is_by_layer()) write_code_int(62, d.seqend_color.aci);
}

// ---------------------------------------------------------------------------
// TRACE
// ---------------------------------------------------------------------------

void DxfWriter::write_trace(const Entity& entity) {
    const auto& d = entity.as_trace();
    write_code(0, "TRACE");
    write_common_entity_codes(entity);
    write_code_double(10, d.p0.x);
    write_code_double(20, d.p0.y);
    write_code_double(30, d.p0.z);
    write_code_double(11, d.p1.x);
    write_code_double(21, d.p1.y);
    write_code_double(31, d.p1.z);
    write_code_double(12, d.p2.x);
    write_code_double(22, d.p2.y);
    write_code_double(32, d.p2.z);
    write_code_double(13, d.p3.x);
    write_code_double(23, d.p3.y);
    write_code_double(33, d.p3.z);
    write_xdata(entity.props.xdata);
}

// ---------------------------------------------------------------------------
// SOLID
// ---------------------------------------------------------------------------

void DxfWriter::write_solid(const Entity& entity) {
    const auto& d = entity.as_solid();
    write_code(0, "SOLID");
    write_common_entity_codes(entity, false);
    write_code_double(10, d.p0.x);
    write_code_double(20, d.p0.y);
    write_code_double(30, d.p0.z);
    write_code_double(11, d.p1.x);
    write_code_double(21, d.p1.y);
    write_code_double(31, d.p1.z);
    write_code_double(12, d.p2.x);
    write_code_double(22, d.p2.y);
    write_code_double(32, d.p2.z);
    write_code_double(13, d.p3.x);
    write_code_double(23, d.p3.y);
    write_code_double(33, d.p3.z);
    write_extrusion_r12(entity);
    write_xdata(entity.props.xdata);
}

// ---------------------------------------------------------------------------
// 3DFACE
// ---------------------------------------------------------------------------

void DxfWriter::write_face3d(const Entity& entity) {
    const auto& d = entity.as_face3d();
    write_code(0, "3DFACE");
    write_common_entity_codes(entity);
    write_code_double(10, d.p0.x);
    write_code_double(20, d.p0.y);
    write_code_double(30, d.p0.z);
    write_code_double(11, d.p1.x);
    write_code_double(21, d.p1.y);
    write_code_double(31, d.p1.z);
    write_code_double(12, d.p2.x);
    write_code_double(22, d.p2.y);
    write_code_double(32, d.p2.z);
    write_code_double(13, d.p3.x);
    write_code_double(23, d.p3.y);
    write_code_double(33, d.p3.z);
    if (d.invisible_edges != 0) {
        write_code_int(70, d.invisible_edges);
    }
    write_xdata(entity.props.xdata);
}

// ---------------------------------------------------------------------------
// SHAPE
// ---------------------------------------------------------------------------

void DxfWriter::write_shape(const Entity& entity) {
    const auto& d = entity.as_shape();
    write_code(0, "SHAPE");
    write_common_entity_codes(entity);
    write_code_double(10, d.insertion_point.x);
    write_code_double(20, d.insertion_point.y);
    write_code_double(30, d.insertion_point.z);
    write_code_double(40, d.size);
    write_code(2, d.name);
    if (d.rotation != 0.0) {
        write_code_double(50, d.rotation);
    }
    if (d.x_scale != 1.0) {
        write_code_double(41, d.x_scale);
    }
    if (d.oblique_angle != 0.0) {
        write_code_double(51, d.oblique_angle);
    }
    write_xdata(entity.props.xdata);
}

// ---------------------------------------------------------------------------
// INSERT
// ---------------------------------------------------------------------------

void DxfWriter::write_insert(const Entity& entity) {
    write_insert_head(entity);
    for (const auto& attr : entity.as_insert().attributes) write_attrib(attr, entity.props.layer);
    write_insert_seqend(entity);
}

void DxfWriter::write_insert_head(const Entity& entity) {
    const auto& d = entity.as_insert();
    write_code(0, "INSERT");
    write_common_entity_codes(entity);

    if (!d.attributes.empty()) {
        write_code_int(66, 1);
    }

    write_code(2, d.block_name);
    write_code_double(10, d.insertion_point.x);
    write_code_double(20, d.insertion_point.y);
    write_code_double(30, d.insertion_point.z);

    if (d.x_scale != 1.0) {
        write_code_double(41, d.x_scale);
    }
    if (d.y_scale != 1.0) {
        write_code_double(42, d.y_scale);
    }
    if (d.z_scale != 1.0) {
        write_code_double(43, d.z_scale);
    }
    if (d.rotation != 0.0) {
        write_code_double(50, d.rotation);
    }
    if (d.column_count > 1) {
        write_code_int(70, d.column_count);
    }
    if (d.row_count > 1) {
        write_code_int(71, d.row_count);
    }
    if (d.column_spacing != 0.0) {
        write_code_double(44, d.column_spacing);
    }
    if (d.row_spacing != 0.0) {
        write_code_double(45, d.row_spacing);
    }
    // XDATA belongs to the INSERT record (it used to follow SEQEND)
    write_xdata(entity.props.xdata);
}

// The attributes follow in records of their own (write_attrib writes an ATTRIB's XDATA, plan
// F20), then SEQEND — also when a file's INSERT says it has attributes and has none
void DxfWriter::write_insert_seqend(const Entity& entity) {
    const auto& d = entity.as_insert();
    if (d.attributes.empty()) {
        // An INSERT the file had with 66 and no attributes keeps its SEQEND, one whose attributes
        // are all removed does not
        const Entity* was = original_of(entity);
        bool kept = r12_template() && !d.seqend_raw.empty() &&
                    (!was || was->type != EntityType::Insert || was->as_insert().attributes.empty());
        if (!kept) return;
    }
    write_code(0, "SEQEND");
    if (!d.seqend_handle.empty()) write_code(5, d.seqend_handle);
    if (entity.props.paper_space != 0) write_code_int(67, entity.props.paper_space);
    write_code(8, entity.props.layer);
}

// ---------------------------------------------------------------------------
// ATTRIB (within INSERT)
// ---------------------------------------------------------------------------

void DxfWriter::write_attrib(const InsertAttribute& attr, const std::string& owner_layer) {
    const auto& d = attr.data;

    write_code(0, "ATTRIB");
    if (!attr.props.handle.empty()) write_code(5, attr.props.handle);
    if (attr.props.paper_space != 0) write_code_int(67, attr.props.paper_space);

    // Use attr props layer if set, otherwise owner layer
    const std::string& layer = attr.props.layer.empty() ? owner_layer : attr.props.layer;
    write_code(8, layer);

    // Write common entity codes from attribute properties
    if (!attr.props.linetype.empty()) {
        write_code(6, attr.props.linetype);
    }
    if (!attr.props.color.is_by_layer()) {
        write_code_int(62, attr.props.color.aci);
    }
    note_r12_only(attr.props);
    if (attr.props.thickness != 0.0) {
        write_code_double(39, attr.props.thickness);
    }
    if (attr.props.extrusion.x != 0.0 ||
        attr.props.extrusion.y != 0.0 ||
        attr.props.extrusion.z != 1.0) {
        write_code_double(210, attr.props.extrusion.x);
        write_code_double(220, attr.props.extrusion.y);
        write_code_double(230, attr.props.extrusion.z);
    }

    write_code_double(10, d.start_point.x);
    write_code_double(20, d.start_point.y);
    write_code_double(30, d.start_point.z);
    write_code_double(40, d.height);
    write_code(1, d.value);
    // The text's own groups and its alignment point, then the attribute's (AutoCAD's R12 file:
    // 10 40 1 11 2 70 74 — its 2000+ order without the subclass markers)
    if (d.rotation != 0.0) {
        write_code_double(50, d.rotation);
    }
    if (d.width_factor != 1.0) {
        write_code_double(41, d.width_factor);
    }
    if (d.oblique_angle != 0.0) {
        write_code_double(51, d.oblique_angle);
    }
    if (d.style != "Standard") {
        write_code(7, d.style);
    }
    if (d.generation_flags != 0) {
        write_code_int(71, d.generation_flags);
    }
    if (d.h_justify != 0) {
        write_code_int(72, d.h_justify);
    }
    if (d.h_justify != 0 || d.v_justify != 0) {
        write_code_double(11, d.alignment_point.x);
        write_code_double(21, d.alignment_point.y);
        write_code_double(31, d.alignment_point.z);
    }
    write_code(2, d.tag);
    write_code_int(70, d.flags);  // Always required by AutoCAD

    if (d.field_length != 0) {
        write_code_int(73, d.field_length);
    }
    if (d.v_justify != 0) {
        write_code_int(74, d.v_justify);
    }
    write_xdata(attr.props.xdata);
    write_attribute_lock(d.lock_position);
}

// ---------------------------------------------------------------------------
// ATTDEF
// ---------------------------------------------------------------------------

void DxfWriter::write_attdef(const Entity& entity) {
    const auto& d = entity.as_attdef();
    write_code(0, "ATTDEF");
    write_common_entity_codes(entity);
    write_code_double(10, d.start_point.x);
    write_code_double(20, d.start_point.y);
    write_code_double(30, d.start_point.z);
    write_code_double(40, d.height);
    write_code(1, d.default_value);
    // The text's own groups and its alignment point, then the attribute's (AutoCAD's R12 file:
    // 10 40 1 11 2 70 74 — its 2000+ order without the subclass markers)
    if (d.rotation != 0.0) {
        write_code_double(50, d.rotation);
    }
    if (d.width_factor != 1.0) {
        write_code_double(41, d.width_factor);
    }
    if (d.oblique_angle != 0.0) {
        write_code_double(51, d.oblique_angle);
    }
    if (d.style != "Standard") {
        write_code(7, d.style);
    }
    if (d.generation_flags != 0) {
        write_code_int(71, d.generation_flags);
    }
    if (d.h_justify != 0) {
        write_code_int(72, d.h_justify);
    }
    if (d.h_justify != 0 || d.v_justify != 0) {
        write_code_double(11, d.alignment_point.x);
        write_code_double(21, d.alignment_point.y);
        write_code_double(31, d.alignment_point.z);
    }
    write_code(3, d.prompt);
    write_code(2, d.tag);
    write_code_int(70, d.flags);

    if (d.field_length != 0) {
        write_code_int(73, d.field_length);
    }
    if (d.v_justify != 0) {
        write_code_int(74, d.v_justify);
    }

    write_xdata(entity.props.xdata);
    write_attribute_lock(d.lock_position);
}

// ---------------------------------------------------------------------------
// ATTRIB (standalone entity)
// ---------------------------------------------------------------------------

void DxfWriter::write_attrib_entity(const Entity& entity) {
    const auto& d = entity.as_attrib();
    write_code(0, "ATTRIB");
    write_common_entity_codes(entity);
    write_code_double(10, d.start_point.x);
    write_code_double(20, d.start_point.y);
    write_code_double(30, d.start_point.z);
    write_code_double(40, d.height);
    write_code(1, d.value);
    // The text's own groups and its alignment point, then the attribute's (AutoCAD's R12 file:
    // 10 40 1 11 2 70 74 — its 2000+ order without the subclass markers)
    if (d.rotation != 0.0) {
        write_code_double(50, d.rotation);
    }
    if (d.width_factor != 1.0) {
        write_code_double(41, d.width_factor);
    }
    if (d.oblique_angle != 0.0) {
        write_code_double(51, d.oblique_angle);
    }
    if (d.style != "Standard") {
        write_code(7, d.style);
    }
    if (d.generation_flags != 0) {
        write_code_int(71, d.generation_flags);
    }
    if (d.h_justify != 0) {
        write_code_int(72, d.h_justify);
    }
    if (d.h_justify != 0 || d.v_justify != 0) {
        write_code_double(11, d.alignment_point.x);
        write_code_double(21, d.alignment_point.y);
        write_code_double(31, d.alignment_point.z);
    }
    write_code(2, d.tag);
    write_code_int(70, d.flags);  // Always required by AutoCAD

    if (d.field_length != 0) {
        write_code_int(73, d.field_length);
    }
    if (d.v_justify != 0) {
        write_code_int(74, d.v_justify);
    }

    write_xdata(entity.props.xdata);
    write_attribute_lock(d.lock_position);
}

// ---------------------------------------------------------------------------
// DIMENSION
// ---------------------------------------------------------------------------

void DxfWriter::write_dimension(const Entity& entity) {
    const auto& d = entity.as_dimension();
    write_code(0, "DIMENSION");
    write_common_entity_codes(entity);

    // Use generated block name if available, otherwise use entity's block name
    auto it = dim_block_names_.find(&entity);
    const std::string& block_name = (it != dim_block_names_.end()) ? it->second : d.block_name;
    write_code(2, block_name);

    // A block built here: 10 and 11 where it put them
    auto built = built_dimensions_.find(&entity);
    const Point3& p10 = built != built_dimensions_.end() ? built->second.definition_point : d.definition_point;
    const Point3& p11 = built != built_dimensions_.end() ? built->second.text_position : d.text_midpoint;
    write_code_double(10, p10.x);
    write_code_double(20, p10.y);
    write_code_double(30, p10.z);
    write_code_double(11, p11.x);
    write_code_double(21, p11.y);
    write_code_double(31, p11.z);

    // 12, the block translation: AutoCAD leaves it out at 0 (ACadSharp's sample_AC1009)
    if (d.block_translation.x != 0.0 || d.block_translation.y != 0.0 || d.block_translation.z != 0.0) {
        write_code_double(12, d.block_translation.x);
        write_code_double(22, d.block_translation.y);
        write_code_double(32, d.block_translation.z);
    }

    // Group 70: flags combined with dim_type bits — left out at 0, as AutoCAD 2025 writes R12
    // (plan F17)
    int type_bits = static_cast<int>(d.dim_type) & 0x0F;
    if ((d.flags | type_bits) != 0) write_code_int(70, d.flags | type_bits);

    if (!d.text_override.empty()) {
        write_code(1, d.text_override);
    }

    // The style after 70 and 1, and not at all for the standard one
    if (!names_equal(d.dim_style, "Standard")) write_code(3, d.dim_style);

    // Definition points — always write for the applicable dimension type
    // Groups 13,23,33: linear, angular, angular3pt, ordinate
    if (d.dim_type == DimensionType::Rotated || d.dim_type == DimensionType::Aligned ||
        d.dim_type == DimensionType::Angular || d.dim_type == DimensionType::Angular3Pt ||
        d.dim_type == DimensionType::Ordinate) {
        write_code_double(13, d.def_point2.x);
        write_code_double(23, d.def_point2.y);
        write_code_double(33, d.def_point2.z);
    }
    // Groups 14,24,34: linear, angular, angular3pt, ordinate
    if (d.dim_type == DimensionType::Rotated || d.dim_type == DimensionType::Aligned ||
        d.dim_type == DimensionType::Angular || d.dim_type == DimensionType::Angular3Pt ||
        d.dim_type == DimensionType::Ordinate) {
        write_code_double(14, d.def_point3.x);
        write_code_double(24, d.def_point3.y);
        write_code_double(34, d.def_point3.z);
    }
    // Groups 15,25,35: diameter, radius, angular, angular3pt
    if (d.dim_type == DimensionType::Diameter || d.dim_type == DimensionType::Radius ||
        d.dim_type == DimensionType::Angular || d.dim_type == DimensionType::Angular3Pt) {
        write_code_double(15, d.def_point4.x);
        write_code_double(25, d.def_point4.y);
        write_code_double(35, d.def_point4.z);
    }
    // Groups 16,26,36: the arc point of a 2-line angular dimension (the 3-point one has none)
    if (d.dim_type == DimensionType::Angular) {
        const Point3& p16 = built != built_dimensions_.end() ? built->second.arc_point : d.arc_point;
        write_code_double(16, p16.x);
        write_code_double(26, p16.y);
        write_code_double(36, p16.z);
    }

    // The leader length of a radius or diameter dimension also at 0
    bool radial = d.dim_type == DimensionType::Diameter || d.dim_type == DimensionType::Radius;
    if (d.leader_length != 0.0 || radial) {
        write_code_double(40, d.leader_length);
    }
    if (d.rotation != 0.0) {
        write_code_double(50, d.rotation);
    }
    if (d.horizontal_dir != 0.0) {
        write_code_double(51, d.horizontal_dir);
    }
    if (d.oblique_angle != 0.0) {
        write_code_double(52, d.oblique_angle);
    }
    if (d.text_rotation != 0.0) {
        write_code_double(53, d.text_rotation);
    }

    write_xdata(entity.props.xdata);
}

// ---------------------------------------------------------------------------
// VIEWPORT
// ---------------------------------------------------------------------------

void DxfWriter::write_viewport_entity(const Entity& entity) {
    const auto& d = entity.as_viewport();
    write_code(0, "VIEWPORT");
    write_common_entity_codes(entity);
    write_code_double(10, d.center.x);
    write_code_double(20, d.center.y);
    write_code_double(30, d.center.z);
    write_code_double(40, d.width);
    write_code_double(41, d.height);
    write_code_int(68, d.status);
    write_code_int(69, d.id);

    // Write user-provided XDATA first
    write_xdata(entity.props.xdata);

    // Check if user already provided ACAD MVIEW XDATA
    bool has_acad_mview = false;
    for (const auto& xd : entity.props.xdata) {
        if (xd.app_name == "ACAD") {
            has_acad_mview = true;
            break;
        }
    }

    // Generate required ACAD MVIEW XDATA per R12 spec if not provided
    if (!has_acad_mview) {
        double view_height = d.height > 0.0 ? d.height : 1.0;

        write_code(1001, "ACAD");
        write_code(1000, "MVIEW");
        write_code(1002, "{");

        // Version number (16 for R11/R12)
        write_code_int(1070, 16);

        // View target point (1010, 1020, 1030)
        write_code_double(1010, 0.0);
        write_code_double(1020, 0.0);
        write_code_double(1030, 0.0);

        // View direction vector (1010, 1020, 1030)
        write_code_double(1010, 0.0);
        write_code_double(1020, 0.0);
        write_code_double(1030, 1.0);

        // View twist angle
        write_code_double(1040, 0.0);
        // View height
        write_code_double(1040, view_height);
        // View center point X, Y
        write_code_double(1040, d.center.x);
        write_code_double(1040, d.center.y);
        // Perspective lens length
        write_code_double(1040, 50.0);
        // Front clip plane Z value
        write_code_double(1040, 0.0);
        // Back clip plane Z value
        write_code_double(1040, 0.0);

        // View mode
        write_code_int(1070, 0);
        // Circle zoom
        write_code_int(1070, 100);
        // Fast zoom setting
        write_code_int(1070, 1);
        // UCSICON setting
        write_code_int(1070, 3);
        // Snap ON/OFF
        write_code_int(1070, 0);
        // Grid ON/OFF
        write_code_int(1070, 0);
        // Snap style
        write_code_int(1070, 0);
        // Snap ISOPAIR
        write_code_int(1070, 0);

        // Snap angle
        write_code_double(1040, 0.0);
        // Snap base point X, Y
        write_code_double(1040, 0.0);
        write_code_double(1040, 0.0);
        // Snap X spacing
        write_code_double(1040, 10.0);
        // Snap Y spacing
        write_code_double(1040, 10.0);
        // Grid X spacing
        write_code_double(1040, 10.0);
        // Grid Y spacing
        write_code_double(1040, 10.0);

        // Hidden in plot flag
        write_code_int(1070, 0);

        // Frozen layer list (empty)
        write_code(1002, "{");
        write_code(1002, "}");

        // End viewport data
        write_code(1002, "}");
    }
}

// ---------------------------------------------------------------------------
// Unknown entity (round-trip)
// ---------------------------------------------------------------------------

void DxfWriter::write_unknown(const Entity& entity) {
    const auto& d = entity.as_unknown();
    write_code(0, d.entity_type);
    for (const auto& [code, value] : d.group_codes) {
        write_code_typed(code, value);
    }
}

// ---------------------------------------------------------------------------
// ENTITIES section
// ---------------------------------------------------------------------------

void DxfWriter::write_entities(const std::vector<Entity>& entities) {
    write_code(0, "SECTION");
    write_code(2, "ENTITIES");

    for (const auto& e : entities) {
        write_entity_r12(e);
    }

    write_code(0, "ENDSEC");
}

// ---------------------------------------------------------------------------
// EOF
// ---------------------------------------------------------------------------

void DxfWriter::write_eof() {
    write_code(0, "EOF");
}

// ---------------------------------------------------------------------------
// Dimension pseudo-block generation
// ---------------------------------------------------------------------------

static Entity make_line_entity(const Point3& start, const Point3& end) {
    Entity e;
    e.type = EntityType::Line;
    e.data = LineData{start, end};
    return e;
}

static Entity make_solid_entity(const Point3& p0, const Point3& p1,
                                 const Point3& p2, const Point3& p3) {
    Entity e;
    e.type = EntityType::Solid;
    e.data = SolidData{p0, p1, p2, p3};
    return e;
}

static Entity make_text_entity(const Point3& pos, const std::string& content,
                                double height, double rotation = 0.0) {
    Entity e;
    e.type = EntityType::Text;
    TextData td;
    td.position = pos;
    td.height = height;
    td.content = content;
    td.rotation = rotation;
    td.h_justify = 1;  // center
    td.v_justify = 2;  // middle
    td.alignment_point = pos;
    e.data = std::move(td);
    return e;
}

// Generate a filled arrowhead SOLID at 'tip' pointing along 'dir'
static Entity make_arrow(const Point3& tip, double dx, double dy, double size) {
    // Arrow is a triangle: tip, and two base points perpendicular to direction
    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-12) return make_solid_entity(tip, tip, tip, tip);
    double ux = dx / len, uy = dy / len;  // unit direction
    double px = -uy, py = ux;             // perpendicular
    double w = size * 0.3;               // arrow half-width

    Point3 base_center = {tip.x - ux * size, tip.y - uy * size, tip.z};
    Point3 left  = {base_center.x + px * w, base_center.y + py * w, tip.z};
    Point3 right = {base_center.x - px * w, base_center.y - py * w, tip.z};

    return make_solid_entity(tip, left, right, right);
}

static std::string format_measurement(double value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", value);
    // Trim trailing zeros
    char* dot = std::strchr(buf, '.');
    if (dot) {
        char* end = buf + std::strlen(buf) - 1;
        while (end > dot + 1 && *end == '0') { *end = '\0'; end--; }
    }
    return buf;
}

DimStyle DxfWriter::find_dim_style(const std::string& name, const Drawing& dwg) const {
    for (const auto& ds : dwg.dim_styles()) {
        if (names_equal(ds.name, name)) return ds;
    }
    return DimStyle{};  // default "Standard"
}

// A block for every DIMENSION without one, as AutoCAD builds it (rättningsfas R.7,
// dimension_block.cpp), named *D<n> past the drawing's own; and the blocks of AutoCAD's
// built-in arrowheads the dimensions and the dimension styles use that the drawing lacks
void DxfWriter::generate_dim_blocks(const Drawing& dwg) {
    dim_block_names_.clear();
    generated_dim_blocks_.clear();
    built_dimensions_.clear();
    TextLayout layout(dwg);
    std::vector<std::string> arrows;
    int counter = 1;

    // The DIMENSIONs the file had without a block (AutoCAD's dynamic constraints, "d1=80"): one
    // that still has none stays without, also when its value is edited
    std::unordered_set<uint64_t> blockless;
    if (original_) {
        auto note = [&](const std::vector<Entity>& entities) {
            for (const auto& e : entities) {
                uint64_t handle = 0;
                if (e.dimension_part() && e.dimension_part()->block_name.empty() &&
                    parse_handle(e.props.handle, handle) == HandleParse::Ok) {
                    blockless.insert(handle);
                }
            }
        };
        note(original_->entities());
        for (const auto& b : original_->blocks()) note(b.entities);
    }

    // A dimensional constraint (a DIMENSION an ACDBASSOCDEPENDENCY depends on whose body is an ASSOCDIMDEPENDENCYBODY or a
    // dynamic block's BLOCKPARAMDEPENDENCYBODY) has no block in AutoCAD's files, also when written from its typed values:
    // a *D block made for it makes AutoCAD discard the drawing ("Invalid Block Name", the 9.S batch)
    std::unordered_set<std::string> constraints;
    for (const auto& o : dwg.objects()) {
        if (o.type != ObjectType::AssocDependency) continue;
        const auto* data = o.data.get_if<Boxed<AssocDependencyData>>();
        if (!data) continue;
        const Object* body = dwg.find_object((**data).dependency.dependency_body);
        if (body && (body->type == ObjectType::AssocDimDependencyBody || body->type == ObjectType::BlockParameterDependencyBody)) {
            constraints.insert((**data).dependency.dependent_on_object);
        }
    }

    auto build = [&](const std::vector<Entity>& entities) {
        for (const auto& e : entities) {
            if (!e.dimension_part()) continue;      // DIMENSION, ARC_DIMENSION and LARGE_RADIAL_DIMENSION (step 9.17)
            const auto& dim = *e.dimension_part();
            // A block of its own stays; a missing one is built (as ODA File Converter does)
            if (!dim.block_name.empty() && dwg.find_block(dim.block_name) != nullptr) continue;
            uint64_t handle = 0;
            if (dim.block_name.empty() && parse_handle(e.props.handle, handle) == HandleParse::Ok && blockless.count(handle) > 0) {
                continue;
            }
            if (dim.block_name.empty() && constraints.count(e.props.handle) > 0) continue;

            std::string name;
            do {
                name = "*D" + std::to_string(counter++);
            } while (dwg.find_block(name) != nullptr);
            dim_block_names_[&e] = name;

            Block blk;
            blk.name = name;
            blk.flags = 1;  // anonymous block
            blk.layer = "0";
            // AutoCAD gives the *D block its dimension's color on BLOCK and on ENDBLK (AutoCAD 2025,
            // fas9_existing_fields.scr: 62 193; a true color as 62 + 420; ByLayer none)
            if (!e.props.color.is_by_layer()) {
                blk.block_color = e.props.color;
                blk.endblk_color = e.props.color;
            }

            DimensionBuild built;
            std::string why;
            if (build_dimension(dwg, layout, e, version_, built, why)) {
                blk.entities = std::move(built.entities);
                built_dimensions_[&e] = {built.definition_point, built.text_position, built.measurement, built.arc_point};
                for (const auto& a : built.arrows) arrows.push_back(a);
            } else {
                dimension_geometry(dim, find_dim_style(dim.dim_style, dwg), blk);
            }
            generated_dim_blocks_.push_back(std::move(blk));
        }
    };
    build(dwg.entities());
    for (const auto& b : dwg.blocks()) build(b.entities);

    for (const auto& ds : dwg.dim_styles()) {
        for (const std::string* name : {&ds.dimblk, &ds.dimblk1, &ds.dimblk2, &ds.dimldrblk}) {
            std::string arrow = builtin_arrow_name(dwg, *name, version_);
            if (is_builtin_arrow(arrow)) arrows.push_back(arrow);
        }
    }
    for (const auto& name : arrows) {
        Block arrow = builtin_arrow_block(name);
        bool present = dwg.find_block(arrow.name) != nullptr ||
                       std::any_of(generated_dim_blocks_.begin(), generated_dim_blocks_.end(),
                                   [&](const Block& b) { return names_equal(b.name, arrow.name); });
        if (present) continue;
        if (version_ < DxfVersion::R2000) older_arrow_block(arrow);
        generated_dim_blocks_.push_back(std::move(arrow));
    }
    for (const auto& w : layout.warnings()) {
        if (std::find(warnings_.begin(), warnings_.end(), w) == warnings_.end()) warnings_.push_back(w);
    }
    // AutoCAD makes the layer Defpoints for a dimension's definition points
    needs_defpoints_ = !built_dimensions_.empty() && !has_entry_named(dwg.layers(), "Defpoints");
}

// Before 2000: BYBLOCK as AutoCAD writes R12 (ACadSharp's sample_AC1009 _ARCHTICK), no lineweights,
// and — R12 and R13 have no LWPOLYLINE — an arrowhead's wide polyline as POLYLINE with its widths
void DxfWriter::older_arrow_block(Block& arrow) {
    for (auto& e : arrow.entities) {
        e.props.linetype = "BYBLOCK";
        e.props.lineweight = -1;
        if (e.type != EntityType::LwPolyline || version_ == DxfVersion::R14) continue;
        const auto& lw = e.as_lwpolyline();
        PolylineData p;
        p.flags = lw.flags & 1;
        if (!lw.vertices.empty()) {
            p.default_start_width = lw.vertices[0].start_width;
            p.default_end_width = lw.vertices[0].end_width;
        }
        for (const auto& v : lw.vertices) {
            Vertex vx;
            vx.position = {v.position.x, v.position.y, 0.0};
            vx.bulge = v.bulge;
            p.vertices.push_back(vx);
        }
        e.type = EntityType::Polyline;
        e.data = std::move(p);
    }
}

// The geometry of a dimension's block, by its type
void DxfWriter::dimension_geometry(const DimensionData& dim, const DimStyle& style, Block& block) {
    switch (dim.dim_type) {
        case DimensionType::Rotated:
        case DimensionType::Aligned:
            generate_linear_dim_geometry(dim, style, block);
            break;
        case DimensionType::Diameter:
        case DimensionType::Radius:
            generate_radial_dim_geometry(dim, style, block);
            break;
        case DimensionType::Angular:
        case DimensionType::Angular3Pt:
            generate_angular_dim_geometry(dim, style, block);
            break;
        case DimensionType::Ordinate:
            generate_ordinate_dim_geometry(dim, style, block);
            break;
    }
}

void DxfWriter::generate_linear_dim_geometry(const DimensionData& dim,
                                              const DimStyle& style,
                                              Block& block) {
    double scale  = style.dimscale;
    double asz    = style.dimasz * scale;
    double exo    = style.dimexo * scale;
    double exe    = style.dimexe * scale;
    double txt_h  = style.dimtxt * scale;
    double gap    = style.dimgap * scale;

    // For Aligned dimensions, compute angle from definition points
    double angle_deg = dim.rotation;
    if (dim.dim_type == DimensionType::Aligned) {
        double dx = dim.def_point3.x - dim.def_point2.x;
        double dy = dim.def_point3.y - dim.def_point2.y;
        angle_deg = std::atan2(dy, dx) * 180.0 / M_PI;
    }
    double angle = angle_deg * M_PI / 180.0;
    double ca = std::cos(angle), sa = std::sin(angle);

    // Direction along dimension line and perpendicular (extension line direction)
    // dir  = along dimension line
    // norm = perpendicular (extension lines go this direction)
    double dir_x = ca, dir_y = sa;

    // The dimension line passes through definition_point
    Point3 dp = dim.definition_point;
    Point3 p1 = dim.def_point2;  // first extension line origin
    Point3 p2 = dim.def_point3;  // second extension line origin

    // Project p1 and p2 onto the dimension line through dp
    double t1 = (p1.x - dp.x) * dir_x + (p1.y - dp.y) * dir_y;
    double t2 = (p2.x - dp.x) * dir_x + (p2.y - dp.y) * dir_y;

    Point3 foot1 = {dp.x + dir_x * t1, dp.y + dir_y * t1, 0.0};
    Point3 foot2 = {dp.x + dir_x * t2, dp.y + dir_y * t2, 0.0};

    // Extension line directions (from origin point towards foot on dim line)
    auto ext_dir = [](const Point3& from, const Point3& to, double& ux, double& uy) {
        double dx = to.x - from.x, dy = to.y - from.y;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-12) { ux = 0; uy = 1; return; }
        ux = dx / len; uy = dy / len;
    };

    double eu1x, eu1y, eu2x, eu2y;
    ext_dir(p1, foot1, eu1x, eu1y);
    ext_dir(p2, foot2, eu2x, eu2y);

    // Extension line 1: from p1+offset to foot1+extension
    Point3 ext1_start = {p1.x + eu1x * exo, p1.y + eu1y * exo, 0.0};
    Point3 ext1_end   = {foot1.x + eu1x * exe, foot1.y + eu1y * exe, 0.0};
    block.entities.push_back(make_line_entity(ext1_start, ext1_end));

    // Extension line 2: from p2+offset to foot2+extension
    Point3 ext2_start = {p2.x + eu2x * exo, p2.y + eu2y * exo, 0.0};
    Point3 ext2_end   = {foot2.x + eu2x * exe, foot2.y + eu2y * exe, 0.0};
    block.entities.push_back(make_line_entity(ext2_start, ext2_end));

    // Measurement
    double measured = std::fabs(t2 - t1);
    std::string text = dim.text_override.empty()
                         ? format_measurement(measured)
                         : dim.text_override;
    if (text == " ") text.clear();  // single space = suppressed

    // Text position (use text_midpoint from the dimension data)
    Point3 text_pos = dim.text_midpoint;
    double text_half_w = txt_h * 0.6 * static_cast<double>(text.size()) * 0.5 + gap;

    // Dimension line: from foot1 to foot2, with gap around text
    // Project text_midpoint onto the dim line to find gap position
    double t_text = (text_pos.x - foot1.x) * dir_x + (text_pos.y - foot1.y) * dir_y;
    double dim_len = t2 - t1;  // can be negative
    double dt1 = 0.0;  // start of dim line in local coords along dir from foot1
    double dt2 = dim_len;

    if (!text.empty()) {
        double gap_start = t_text - text_half_w;
        double gap_end   = t_text + text_half_w;

        // Left segment (foot1 to gap_start)
        if (std::fabs(gap_start - dt1) > asz) {
            Point3 ls = foot1;
            Point3 le = {foot1.x + dir_x * gap_start, foot1.y + dir_y * gap_start, 0.0};
            block.entities.push_back(make_line_entity(ls, le));
        }
        // Right segment (gap_end to foot2)
        if (std::fabs(dt2 - gap_end) > asz) {
            Point3 rs = {foot1.x + dir_x * gap_end, foot1.y + dir_y * gap_end, 0.0};
            Point3 re = foot2;
            block.entities.push_back(make_line_entity(rs, re));
        }
    } else {
        // No text, single dimension line
        block.entities.push_back(make_line_entity(foot1, foot2));
    }

    // Arrowheads at foot1 and foot2
    // Arrow at foot1 pointing towards foot2
    block.entities.push_back(make_arrow(foot1,
        (t2 > t1 ? dir_x : -dir_x), (t2 > t1 ? dir_y : -dir_y), asz));
    // Arrow at foot2 pointing towards foot1
    block.entities.push_back(make_arrow(foot2,
        (t2 > t1 ? -dir_x : dir_x), (t2 > t1 ? -dir_y : dir_y), asz));

    // Text
    if (!text.empty()) {
        block.entities.push_back(make_text_entity(text_pos, text, txt_h, angle_deg));
    }
}

void DxfWriter::generate_radial_dim_geometry(const DimensionData& dim,
                                              const DimStyle& style,
                                              Block& block) {
    double scale = style.dimscale;
    double asz   = style.dimasz * scale;
    double txt_h = style.dimtxt * scale;

    // For Diameter: definition_point is across from def_point4 (pick point)
    // For Radius:   definition_point is the center, def_point4 is on the circle
    Point3 center, on_circle;
    if (dim.dim_type == DimensionType::Diameter) {
        center    = {(dim.definition_point.x + dim.def_point4.x) / 2.0,
                     (dim.definition_point.y + dim.def_point4.y) / 2.0, 0.0};
        on_circle = dim.def_point4;
    } else {
        center    = dim.definition_point;
        on_circle = dim.def_point4;
    }

    // Direction from center to circle point
    double dx = on_circle.x - center.x;
    double dy = on_circle.y - center.y;
    double dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 1e-12) return;

    double ux = dx / dist, uy = dy / dist;
    double angle_deg = std::atan2(uy, ux) * 180.0 / M_PI;

    // Leader line
    Point3 line_start, line_end;
    if (dim.dim_type == DimensionType::Diameter) {
        line_start = dim.definition_point;  // opposite side
        line_end   = on_circle;
    } else {
        line_start = center;
        line_end   = {on_circle.x + ux * dim.leader_length,
                      on_circle.y + uy * dim.leader_length, 0.0};
    }
    block.entities.push_back(make_line_entity(line_start, line_end));

    // Arrow at the circle point, pointing outward
    block.entities.push_back(make_arrow(on_circle, ux, uy, asz));
    if (dim.dim_type == DimensionType::Diameter) {
        // Second arrow on opposite side, pointing inward
        block.entities.push_back(make_arrow(dim.definition_point, -ux, -uy, asz));
    }

    // Measurement text
    double measured = (dim.dim_type == DimensionType::Diameter) ? dist * 2.0 : dist;
    std::string prefix = (dim.dim_type == DimensionType::Diameter) ? "%%c" : "R";
    std::string text = dim.text_override.empty()
                         ? prefix + format_measurement(measured)
                         : dim.text_override;

    if (!text.empty() && text != " ") {
        block.entities.push_back(make_text_entity(dim.text_midpoint, text, txt_h, angle_deg));
    }
}

void DxfWriter::generate_angular_dim_geometry(const DimensionData& dim,
                                               const DimStyle& style,
                                               Block& block) {
    double scale = style.dimscale;
    double asz   = style.dimasz * scale;
    double txt_h = style.dimtxt * scale;

    Point3 vertex, p1_end, p2_end, arc_pt;

    if (dim.dim_type == DimensionType::Angular3Pt) {
        // Angular 3-point: vertex at def_point4, lines to def_point2 and def_point3
        vertex = dim.def_point4;
        p1_end = dim.def_point2;
        p2_end = dim.def_point3;
        arc_pt = dim.arc_point;
    } else {
        // Angular: two lines defined by (def_point2, definition_point) and (def_point3, def_point4)
        // Compute intersection as vertex
        // Simplified: use arc_point direction from definition_point
        vertex = dim.definition_point;
        p1_end = dim.def_point2;
        p2_end = dim.def_point3;
        arc_pt = dim.arc_point;
    }

    // Extension lines from vertex to the two points
    block.entities.push_back(make_line_entity(vertex, p1_end));
    block.entities.push_back(make_line_entity(vertex, p2_end));

    // Compute angles
    double a1 = std::atan2(p1_end.y - vertex.y, p1_end.x - vertex.x);
    double a2 = std::atan2(p2_end.y - vertex.y, p2_end.x - vertex.x);

    // Arc radius (distance from vertex to arc_point)
    double arc_r = std::sqrt(std::pow(arc_pt.x - vertex.x, 2) +
                             std::pow(arc_pt.y - vertex.y, 2));
    if (arc_r < 1e-12) arc_r = 20.0;

    // Generate arc as line segments (approximate)
    double start_a = a1, end_a = a2;
    if (end_a < start_a) end_a += 2.0 * M_PI;
    int segments = 16;
    double step = (end_a - start_a) / segments;

    for (int i = 0; i < segments; ++i) {
        double aa = start_a + step * i;
        double ab = start_a + step * (i + 1);
        Point3 pa = {vertex.x + arc_r * std::cos(aa), vertex.y + arc_r * std::sin(aa), 0.0};
        Point3 pb = {vertex.x + arc_r * std::cos(ab), vertex.y + arc_r * std::sin(ab), 0.0};
        block.entities.push_back(make_line_entity(pa, pb));
    }

    // Arrowheads at arc ends
    Point3 arc_start = {vertex.x + arc_r * std::cos(start_a), vertex.y + arc_r * std::sin(start_a), 0.0};
    Point3 arc_end   = {vertex.x + arc_r * std::cos(end_a), vertex.y + arc_r * std::sin(end_a), 0.0};
    // Tangent at start (perpendicular to radius, CCW)
    block.entities.push_back(make_arrow(arc_start, -std::sin(start_a), std::cos(start_a), asz));
    // Tangent at end (perpendicular to radius, CW)
    block.entities.push_back(make_arrow(arc_end, std::sin(end_a), -std::cos(end_a), asz));

    // Measurement text (angle in degrees)
    double angle_measured = (end_a - start_a) * 180.0 / M_PI;
    std::string text = dim.text_override.empty()
                         ? format_measurement(angle_measured) + "%%d"
                         : dim.text_override;

    if (!text.empty() && text != " ") {
        double mid_a = (start_a + end_a) / 2.0;
        Point3 text_pos = dim.text_midpoint;
        double text_angle = mid_a * 180.0 / M_PI;
        block.entities.push_back(make_text_entity(text_pos, text, txt_h, text_angle));
    }
}

void DxfWriter::generate_ordinate_dim_geometry(const DimensionData& dim,
                                                const DimStyle& style,
                                                Block& block) {
    double scale = style.dimscale;
    double txt_h = style.dimtxt * scale;

    Point3 feature = dim.def_point2;  // the feature point
    Point3 leader  = dim.def_point3;  // the leader endpoint

    // Leader line (with optional kink)
    block.entities.push_back(make_line_entity(feature, leader));

    // Determine if X or Y ordinate (bit 6 of flags)
    bool is_x = (dim.flags & 64) != 0;
    double value = is_x ? feature.x : feature.y;

    std::string text = dim.text_override.empty()
                         ? format_measurement(value)
                         : dim.text_override;

    if (!text.empty() && text != " ") {
        block.entities.push_back(make_text_entity(dim.text_midpoint, text, txt_h));
    }
}

} // namespace internal
} // namespace hdxf
