#include "hdxf.hpp"
#include "handles.hpp"
#include "standard_objects.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>

// The records a new R13 or R14 drawing has (plan phase 7, step 7.5, B89/B97, format profile 9 and D1):
// the model and paper space blocks with their BLOCK_RECORDs, the root dictionary with ACAD_GROUP and
// ACAD_MLINESTYLE and the STANDARD multiline style, in the form of AutoCAD's own R13 and R14 files
// (family N: libre/r13_v.dxf, spatialauxetic/HEAD.DXF): no XDATA on the dictionaries, no owner 330
// (the writer leaves it out, DxfWriter::owner_groups_written()), the owner only as the reactor of
// what a dictionary owns. Their ACAD_LAYOUT, ACAD_PLOTSETTINGS and ACAD_PLOTSTYLENAME hold what
// the drawing's layouts and plot styles are for in later versions; a new drawing has none of it (R13
// and R14 have one paper space, no layout tabs). The XDATA ACAD TREAT_AS_HARD of the root dictionary
// is family A2000's (AutoCAD 2002 … 2005 saving as R13: ezdxf/small_r13.dxf), kept when read.

namespace hdxf {

namespace {

bool same_name(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) !=
            std::toupper(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

// Whether a layout still has the values a new 2000 drawing gives its layouts (handles and the extents the writer keeps aside): one the
// program changed is its own and stays (Codex review, phase 7)
bool same_point(const Point2& a, const Point2& b) { return a.x == b.x && a.y == b.y; }
bool same_point(const Point3& a, const Point3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

bool same_plot_settings(const PlotSettings& a, const PlotSettings& b) {
    return a.page_setup_name == b.page_setup_name && a.printer_name == b.printer_name &&
           a.paper_size == b.paper_size && a.plot_view_name == b.plot_view_name &&
           a.margin_left == b.margin_left && a.margin_bottom == b.margin_bottom &&
           a.margin_right == b.margin_right && a.margin_top == b.margin_top &&
           a.paper_width == b.paper_width && a.paper_height == b.paper_height &&
           same_point(a.plot_origin, b.plot_origin) && same_point(a.window_min, b.window_min) &&
           same_point(a.window_max, b.window_max) && a.scale_numerator == b.scale_numerator &&
           a.scale_denominator == b.scale_denominator && a.flags == b.flags &&
           a.paper_units == b.paper_units && a.rotation == b.rotation && a.plot_type == b.plot_type &&
           a.style_sheet == b.style_sheet && a.standard_scale_type == b.standard_scale_type &&
           a.shade_plot_mode == b.shade_plot_mode && a.shade_plot_resolution == b.shade_plot_resolution &&
           a.shade_plot_dpi == b.shade_plot_dpi && a.scale_factor == b.scale_factor &&
           same_point(a.image_origin, b.image_origin);
}

bool same_layout_values(const LayoutData& a, const LayoutData& b) {
    return same_plot_settings(a.plot_settings, b.plot_settings) && same_name(a.name, b.name) &&
           a.flags == b.flags && a.tab_order == b.tab_order && same_point(a.limits_min, b.limits_min) &&
           same_point(a.limits_max, b.limits_max) && same_point(a.insertion_base, b.insertion_base) &&
           a.elevation == b.elevation && same_point(a.ucs_origin, b.ucs_origin) &&
           same_point(a.ucs_x_axis, b.ucs_x_axis) && same_point(a.ucs_y_axis, b.ucs_y_axis) &&
           a.ucs_ortho_type == b.ucs_ortho_type && a.ucs_handle.empty() && a.base_ucs_handle.empty() &&
           a.thumbnail.empty();
}


// A dictionary as R13 and R14 have it: no duplicate record cloning flag (281), which is 2000's
DictionaryData older_dictionary(std::vector<DictionaryEntry> entries) {
    DictionaryData d;
    d.cloning = 0;
    d.entries = std::move(entries);
    return d;
}

Object make_object(ObjectType type, const std::string& handle, const std::string& owner,
                   ObjectData data) {
    Object o;
    o.type = type;
    o.handle = handle;
    o.owner = owner;
    if (owner != "0") o.reactors.push_back(owner);
    o.data = std::move(data);
    return o;
}

// ACAD / TREAT_AS_HARD / 0: what 2000 and family A2000 put on the root dictionary and the two it owns
bool is_treat_as_hard(const std::vector<XdataList>& xdata) {
    if (xdata.size() != 1 || xdata[0].app_name != "ACAD" || xdata[0].items.size() != 2) return false;
    return xdata[0].items[0].code == 1000 && xdata[0].items[0].value == "TREAT_AS_HARD" &&
           xdata[0].items[1].code == 1070 && std::atoi(xdata[0].items[1].value.c_str()) == 0;
}

MlineStyleData standard_mline_style() {
    MlineStyleData d;
    d.name = "STANDARD";
    d.elements = {{0.5, Color::by_layer(), "BYLAYER"}, {-0.5, Color::by_layer(), "BYLAYER"}};
    return d;
}

Object* root_of(std::vector<Object>& objects) {
    for (auto& o : objects) {
        if (o.type == ObjectType::Dictionary && o.owner == "0") return &o;
    }
    return nullptr;
}

Object* find_by_handle(std::vector<Object>& objects, const std::string& handle) {
    for (auto& o : objects) {
        if (o.handle == handle) return &o;
    }
    return nullptr;
}

const DictionaryEntry* entry_named(const Object& dict, const std::string& name) {
    for (const auto& e : dict.as_dictionary().entries) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

// The block of that name (the API's *Model_Space, *Paper_Space, whatever the case)
Block* block_named(std::vector<Block>& blocks, const char* name) {
    for (auto& b : blocks) {
        if (same_name(b.name, name)) return &b;
    }
    return nullptr;
}

} // namespace

void Drawing::ensure_older_structure() {
    // A drawing read from an R13 or later file has its records as they were read — one with no
    // objects at all gets the base structure, as ensure_r2000_structure() gives one of 2000
    if (source_version_ != DxfVersion::R12 && !objects_.empty()) return;

    if (!objects_.empty()) {
        // The standard materials and visual styles of a drawing made as 2007 or later go too (R13 and R14 have no
        // ACAD_MATERIAL or ACAD_VISUALSTYLE; step 9.7): the ones that are still AutoCAD's
        internal::drop_standard_style_objects(*this);
        // 2000's starting values go (a drawing made as 2000 and then set to R13 or R14); R13 and
        // R14 differ in the XDATA of the dictionaries the drawing's structure made
        Block* model = block_named(blocks_, "*Model_Space");
        Block* paper = block_named(blocks_, "*Paper_Space");
        Block* paper0 = block_named(blocks_, "*Paper_Space0");
        Object* root = root_of(objects_);
        if (!root || !model || !paper) return;

        // The layout graph AutoCAD's 2000 starting drawing has: Model, Layout1 and Layout2, each with
        // its own BLOCK_RECORD (Layout2's *Paper_Space0 empty), the ACAD_LAYOUT dictionary, an empty
        // ACAD_PLOTSETTINGS and ACAD_PLOTSTYLENAME with only its Normal. A layout of the program's, or
        // something else in them, stays — and the drawing is refused when it is written.
        // (a layout is AutoCAD's own when it has the name and the BLOCK_RECORD it was made with)
        std::vector<std::pair<std::string, const char*>> standard = {
            {model->record.handle, "Model"}, {paper->record.handle, "Layout1"}};
        if (paper0 && paper0->entities.empty()) standard.push_back({paper0->record.handle, "Layout2"});
        const Drawing fresh(DxfVersion::R2000);   // the layouts as a new 2000 drawing has them
        bool own_layout = false;
        for (const auto& o : objects_) {
            if (o.type != ObjectType::Layout) continue;
            bool made_here = std::any_of(standard.begin(), standard.end(), [&](const auto& s) {
                if (s.first != o.as_layout().block_record_handle || !same_name(s.second, o.as_layout().name)) return false;
                for (const auto& f : fresh.objects()) {
                    if (f.type == ObjectType::Layout && same_name(f.as_layout().name, s.second))
                        return same_layout_values(o.as_layout(), f.as_layout());
                }
                return false;
            });
            if (!made_here) own_layout = true;
        }
        std::set<std::string> gone;
        if (!own_layout) {
            for (const auto& o : objects_) {
                if (o.type == ObjectType::Layout) gone.insert(o.handle);
            }
            auto drop_if_only = [&](const char* name, auto only_these) {
                const DictionaryEntry* e = entry_named(*root, name);
                if (!e) return;
                Object* dict = find_by_handle(objects_, e->handle);
                if (!dict) return;
                if (only_these(*dict, gone)) gone.insert(dict->handle);
            };
            // Empty once the layouts are gone
            drop_if_only("ACAD_LAYOUT", [](const Object& d, const std::set<std::string>& g) {
                if (d.type != ObjectType::Dictionary) return false;
                for (const auto& e : d.as_dictionary().entries) if (!g.count(e.handle)) return false;
                return true;
            });
            drop_if_only("ACAD_PLOTSETTINGS", [](const Object& d, const std::set<std::string>&) {
                return d.type == ObjectType::Dictionary && d.as_dictionary().entries.empty();
            });
            // The plot style dictionary: the Normal placeholder only
            const DictionaryEntry* styles = entry_named(*root, "ACAD_PLOTSTYLENAME");
            if (styles) {
                Object* dict = find_by_handle(objects_, styles->handle);
                if (dict && dict->type == ObjectType::DictionaryWithDefault) {
                    const auto& d = dict->as_dictionary_with_default();
                    Object* normal = d.dictionary.entries.size() == 1
                                         ? find_by_handle(objects_, d.dictionary.entries[0].handle) : nullptr;
                    if (normal && normal->type == ObjectType::Placeholder &&
                        d.dictionary.entries[0].handle == d.default_handle) {
                        gone.insert(dict->handle);
                        gone.insert(normal->handle);
                    }
                }
            }
            // Only the ones the drawing's own structure made leave the root (and its handle)
            auto& entries = root->as_dictionary().entries;
            entries.erase(std::remove_if(entries.begin(), entries.end(),
                                         [&](const DictionaryEntry& e) { return gone.count(e.handle) > 0; }),
                          entries.end());
            objects_.erase(std::remove_if(objects_.begin(), objects_.end(),
                                          [&](const Object& o) { return gone.count(o.handle) > 0; }),
                           objects_.end());
            model->record.layout_handle.clear();
            paper->record.layout_handle.clear();
            if (paper0 && paper0->entities.empty() && gone.count(paper0->record.layout_handle)) {
                blocks_.erase(blocks_.begin() + (paper0 - blocks_.data()));
            } else if (paper0 && gone.count(paper0->record.layout_handle)) {
                paper0->record.layout_handle.clear();
            }
            rebuild_block_index();
            // The pointers above are stale: found again
            root = root_of(objects_);
        }

        // Family N has no XDATA on the root dictionary and the two it owns: 2000's go — only on
        // the dictionaries that have nothing else of their own
        if (root) {
            std::vector<Object*> dictionaries = {root};
            for (const char* name : {"ACAD_GROUP", "ACAD_MLINESTYLE"}) {
                const DictionaryEntry* e = entry_named(*root, name);
                if (e) {
                    if (Object* d = find_by_handle(objects_, e->handle)) dictionaries.push_back(d);
                }
            }
            for (Object* d : dictionaries) {
                if (d->type == ObjectType::Dictionary && is_treat_as_hard(d->xdata)) d->xdata.clear();
            }
        }
        return;
    }

    scan_handles();   // next_handle_ above every handle in use

    // *Model_Space and *Paper_Space come first, as in AutoCAD's files
    if (!block_named(blocks_, "*Paper_Space")) {
        Block paper;
        paper.name = "*Paper_Space";
        blocks_.insert(blocks_.begin(), std::move(paper));
    }
    if (!block_named(blocks_, "*Model_Space")) {
        Block model;
        model.name = "*Model_Space";
        blocks_.insert(blocks_.begin(), std::move(model));
    }
    rebuild_block_index();
    Block& model = *block_named(blocks_, "*Model_Space");
    Block& paper = *block_named(blocks_, "*Paper_Space");

    std::string root = next_handle();
    std::string groups = next_handle();
    std::string mline_styles = next_handle();
    std::string standard_mline = next_handle();
    if (model.record.handle.empty()) model.record.handle = next_handle();
    if (paper.record.handle.empty()) paper.record.handle = next_handle();
    model.record.layout_handle.clear();
    paper.record.layout_handle.clear();

    // Owners before what they own; dictionary entries sorted by name, as AutoCAD does
    objects_.push_back(make_object(ObjectType::Dictionary, root, "0", older_dictionary({
        {"ACAD_GROUP", groups, false},
        {"ACAD_MLINESTYLE", mline_styles, false},
    })));
    objects_.push_back(make_object(ObjectType::Dictionary, groups, root, older_dictionary({})));
    objects_.push_back(make_object(ObjectType::Dictionary, mline_styles, root, older_dictionary({
        {"STANDARD", standard_mline, false},
    })));
    objects_.push_back(make_object(ObjectType::MlineStyle, standard_mline, mline_styles,
                                   standard_mline_style()));
}

} // namespace hdxf
