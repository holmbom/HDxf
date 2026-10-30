#include "hdxf.hpp"
#include "handles.hpp"
#include "dxf_writer.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <set>

// The records every DXF 2000 drawing has (plan phase 1, step 1.2): *Model_Space and
// *Paper_Space with their BLOCK_RECORDs, a LAYOUT for each, and the standard objects
// AutoCAD writes for a new drawing. The values are AutoCAD's own (ACadSharp's
// sample_AC1015_ascii.dxf, written by AutoCAD).

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

// A name as R13 and R14 write one: upper case, and every character that is not a letter, a digit,
// $, _ or - an underscore, one per character (AutoCAD's R14 file with the image "sc[3].png" names
// its IMAGEDEF "SC_3_")
std::string older_symbol_name(const std::string& name) {
    std::string out;
    for (unsigned char c : name) {
        if ((c & 0xC0) == 0x80) continue;           // the rest of a UTF-8 character
        if (c < 0x80 && (std::isalnum(c) || c == '$' || c == '_' || c == '-')) {
            out += static_cast<char>(std::toupper(c));
        } else {
            out += '_';
        }
    }
    return out;
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

DictionaryData dictionary(std::vector<DictionaryEntry> entries) {
    DictionaryData d;
    d.entries = std::move(entries);
    return d;
}

// AutoCAD's page setup for a new drawing: no plotter, Letter paper, 1:1
PlotSettings default_plot_settings(bool model) {
    PlotSettings p;
    p.printer_name = "none_device";
    p.paper_size = model ? "ANSI_A_(8.50_x_11.00_Inches)" : "Letter_(8.50_x_11.00_Inches)";
    p.margin_left = 6.349999904632568;
    p.margin_bottom = model ? 19.04999923706055 : 6.349999904632568;
    p.margin_right = 6.350006103515625;
    p.margin_top = model ? 19.04998779296875 : 6.350006103515625;
    p.paper_width = 215.8999938964844;
    p.paper_height = 279.3999938964844;
    p.flags = model ? 11952 : 688;
    p.rotation = 1;
    p.plot_type = model ? 0 : 5;
    p.standard_scale_type = model ? 0 : 16;
    return p;
}

LayoutData layout(const std::string& name, int tab_order, const std::string& record) {
    LayoutData l;
    l.plot_settings = default_plot_settings(tab_order == 0);
    l.name = name;
    l.flags = 1;
    l.tab_order = tab_order;
    l.limits_max = {12.0, 9.0};
    l.extents_min = {1e20, 1e20, 1e20};
    l.extents_max = {-1e20, -1e20, -1e20};
    l.block_record_handle = record;
    return l;
}

// AutoCAD's Standard multiline style: two lines 1 apart, ByLayer
MlineStyleData standard_mline_style() {
    MlineStyleData d;
    d.name = "STANDARD";
    d.elements = {{0.5, Color::by_layer(), "BYLAYER"}, {-0.5, Color::by_layer(), "BYLAYER"}};
    return d;
}

Point3 unit3(const Point3& v) {
    double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return len > 0.0 ? Point3{v.x / len, v.y / len, v.z / len} : Point3{};
}

// A multiline's geometry as AutoCAD stores it: each vertex has the direction of the
// segment from it (the last one of an open line, the one before), a miter along the sum
// of the two segments' left normals, and per element its distance from the vertex along
// the miter — the element's offset (moved by the justification) times the scale, over
// the miter's part along the left normal
void mline_geometry(MlineData& d, const std::vector<double>& offsets) {
    size_t n = d.vertices.size();
    if (n == 0) return;
    bool closed = d.is_closed();
    auto segment = [&](size_t a, size_t b) {
        const Point3& p = d.vertices[a].position;
        const Point3& q = d.vertices[b].position;
        return unit3({q.x - p.x, q.y - p.y, q.z - p.z});
    };
    auto left = [](const Point3& v) { return Point3{-v.y, v.x, 0.0}; };
    double top = offsets.empty() ? 0.0 : *std::max_element(offsets.begin(), offsets.end());
    double bottom = offsets.empty() ? 0.0 : *std::min_element(offsets.begin(), offsets.end());
    double shift = d.justification == 0 ? -top : (d.justification == 2 ? -bottom : 0.0);
    for (size_t i = 0; i < n; ++i) {
        Point3 out = n < 2 ? Point3{1.0, 0.0, 0.0}
                           : i + 1 < n ? segment(i, i + 1) : closed ? segment(n - 1, 0) : segment(n - 2, n - 1);
        Point3 in = i > 0 ? segment(i - 1, i) : (closed && n > 1 ? segment(n - 1, 0) : out);
        Point3 a = left(in);
        Point3 b = left(out);
        Point3 miter = unit3({a.x + b.x, a.y + b.y, 0.0});
        if (miter.x == 0.0 && miter.y == 0.0) miter = b;
        double along = miter.x * b.x + miter.y * b.y;
        MlineVertex& v = d.vertices[i];
        v.direction = out;
        v.miter = miter;
        v.elements.clear();
        for (double offset : offsets) {
            MlineElementParams e;
            e.params = {along != 0.0 ? (offset + shift) * d.scale / along : 0.0, 0.0};
            v.elements.push_back(e);
        }
    }
    d.start_point = d.vertices.front().position;
    d.style_elements = static_cast<int>(offsets.size());
}

} // namespace

// A new GUID as AutoCAD writes them: {8-4-4-4-12} upper-case hex, random (version 4)
std::string internal::new_guid() {
    static std::mt19937_64 rng{std::random_device{}()};
    uint64_t hi = rng();
    uint64_t lo = rng();
    hi = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;   // version 4
    lo = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;   // variant
    char buf[40];
    std::snprintf(buf, sizeof(buf), "{%08X-%04X-%04X-%04X-%012llX}",
                  static_cast<unsigned>(hi >> 32), static_cast<unsigned>((hi >> 16) & 0xFFFF),
                  static_cast<unsigned>(hi & 0xFFFF), static_cast<unsigned>(lo >> 48),
                  static_cast<unsigned long long>(lo & 0xFFFFFFFFFFFFULL));
    return buf;
}

using internal::new_guid;

std::string internal::login_name() {
#ifdef _WIN32
    if (const wchar_t* name = _wgetenv(L"USERNAME")) return utf8_from_path(std::filesystem::path(name));
#endif
    if (const char* name = std::getenv("USER")) return name;
    if (const char* name = std::getenv("LOGNAME")) return name;
    return {};
}

namespace internal {

void for_each_color(const Drawing& dwg, const std::function<void(const Color&, bool entity)>& f) {
    for (const auto& l : dwg.layers()) f(l.color, false);
    auto entities = [&](const std::vector<Entity>& list) {
        for (const auto& e : list) {
            f(e.props.color, true);
            if (e.type == EntityType::Insert) {
                for (const auto& a : e.as_insert().attributes) f(a.props.color, true);
            }
            if (e.type == EntityType::MText && e.as_mtext().background.flags != 0) {
                f(e.as_mtext().background.color, true);
            }
            if (e.type == EntityType::Hatch && e.as_hatch().gradient) {
                for (const auto& c : e.as_hatch().gradient->colors) f(c.color, true);
            }
        }
    };
    entities(dwg.entities());
    // A block's colors are its BLOCK's and its ENDBLK's, entities of the file (each when it has one)
    for (const auto& b : dwg.blocks()) {
        if (b.block_color) f(*b.block_color, true);
        if (b.endblk_color) f(*b.endblk_color, true);
        entities(b.entities);
    }
}

} // namespace internal

static void add_entry(DictionaryData& dict, const std::string& name, const std::string& handle, bool sorted = true);

// Plan B21: a color book color an entity has is a DBCOLOR in ACAD_COLOR, under its name —
// made here when there is none, before handles are given out. A layer's color book color
// has none (AutoCAD's 2004 file: Layer_color_book's "DIC COLOR GUIDE(R)$DIC 7" is not in
// ACAD_COLOR, CIRCLE 99F's "RAL CLASSIC$RAL 1006" is). A name with no RGB, with two, or
// with another RGB than its DBCOLOR is an error.
std::string Drawing::ensure_color_objects(std::vector<Object>& backup, bool& added) {
    added = false;
    std::vector<std::pair<std::string, int>> used;     // name → RGB, in the order first used
    std::vector<std::string> on_entities;
    std::string error;
    internal::for_each_color(*this, [&](const Color& c, bool entity) {
        if (c.name.empty() || !error.empty()) return;
        auto named = [&](const std::string& n) { return same_name(n, c.name); };
        if (entity && std::none_of(on_entities.begin(), on_entities.end(), named)) {
            on_entities.push_back(c.name);
        }
        if (!c.has_rgb()) {
            error = "Color \"" + c.name + "\" from a color book has no RGB value";
            return;
        }
        for (const auto& [name, rgb] : used) {
            if (!same_name(name, c.name)) continue;
            if (rgb != c.rgb) error = "Color \"" + c.name + "\" is used with two RGB values";
            return;
        }
        used.push_back({c.name, c.rgb});
    });
    if (!error.empty() || used.empty()) return error;

    Object* root = nullptr;
    for (auto& o : objects_) {
        if (o.type == ObjectType::Dictionary && o.owner == "0") { root = &o; break; }
    }
    if (!root) return {};
    const Object* colors = nullptr;
    for (const auto& entry : root->as_dictionary().entries) {
        if (entry.name == "ACAD_COLOR") colors = find_object(entry.handle);
    }
    std::vector<std::pair<std::string, int>> missing;
    for (const auto& [name, rgb] : used) {
        const Object* existing = nullptr;
        if (colors && colors->type == ObjectType::Dictionary) {
            for (const auto& entry : colors->as_dictionary().entries) {
                if (same_name(entry.name, name)) existing = find_object(entry.handle);
            }
        }
        if (!existing) {
            auto named = [&](const std::string& n) { return same_name(n, name); };
            if (std::any_of(on_entities.begin(), on_entities.end(), named)) missing.push_back({name, rgb});
        } else if (existing->type == ObjectType::DbColor && existing->as_db_color().color.has_rgb() &&
                   existing->as_db_color().color.rgb != rgb) {
            return "Color \"" + name + "\" is used with another RGB value than its DBCOLOR";
        }
    }
    if (missing.empty()) return {};

    backup = objects_;
    added = true;
    scan_handles();
    std::string dict_handle = named_dictionary("ACAD_COLOR");
    for (const auto& [name, rgb] : missing) {
        std::string handle = next_handle();
        if (Object* dict = find_object(dict_handle)) add_entry(dict->as_dictionary(), name, handle);
        DbColorData d;
        d.color = Color::from_book(name, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
        objects_.push_back(make_object(ObjectType::DbColor, handle, dict_handle, d));
    }
    return {};
}

void Drawing::ensure_r2000_structure() {
    // An R13 or R14 drawing has its own, older structure: no layouts (plan phase 7, B89)
    if (version_ == DxfVersion::R13 || version_ == DxfVersion::R14) {
        ensure_older_structure();
        return;
    }
    // A drawing that has objects — read from a file, or made here before — is left alone, except
    // one built in memory with R13's structure: it gets 2000's layouts
    if (!objects_.empty()) {
        if (source_version_ == DxfVersion::R12) add_layout_structure();
        return;
    }
    scan_handles();   // next_handle_ above every handle in use

    // A new drawing's identity ($FINGERPRINTGUID, kept) and version ($VERSIONGUID)
    for (const char* name : {"$FINGERPRINTGUID", "$VERSIONGUID"}) {
        const HeaderVar* h = find_header(name);
        if (!h || h->values.empty() || h->values[0].second.empty()) {
            set_header_string(name, 2, new_guid());
        }
    }

    // *Model_Space and *Paper_Space come first, as in AutoCAD's files
    auto find = [&](const char* name) -> Block* {
        for (auto& b : blocks_) {
            if (same_name(b.name, name)) return &b;
        }
        return nullptr;
    };
    if (!find("*Paper_Space")) {
        Block paper;
        paper.name = "*Paper_Space";
        blocks_.insert(blocks_.begin(), std::move(paper));
    }
    if (!find("*Model_Space")) {
        Block model;
        model.name = "*Model_Space";
        blocks_.insert(blocks_.begin(), std::move(model));
    }
    rebuild_block_index();
    Block& model = *find("*Model_Space");
    Block& paper = *find("*Paper_Space");

    std::string root = next_handle();
    std::string groups = next_handle();
    std::string layouts = next_handle();
    std::string mline_styles = next_handle();
    std::string plot_settings = next_handle();
    std::string plot_styles = next_handle();
    std::string model_layout = next_handle();
    std::string paper_layout = next_handle();
    std::string standard_mline = next_handle();
    std::string normal = next_handle();
    if (model.record.handle.empty()) model.record.handle = next_handle();
    if (paper.record.handle.empty()) paper.record.handle = next_handle();
    model.record.layout_handle = model_layout;
    paper.record.layout_handle = paper_layout;

    // Layout2 with its own paper space block, as in AutoCAD's new drawing (rättningsfas
    // R.3) — unless the drawing already has a block by that name
    // (the blocks move when one is inserted: the references above are not used after it)
    const std::string model_record = model.record.handle;
    const std::string paper_record = paper.record.handle;
    std::string second_layout;
    std::string second_record;
    if (!find("*Paper_Space0")) {
        second_layout = next_handle();
        Block second;
        second.name = "*Paper_Space0";
        second.record.handle = next_handle();
        second.record.layout_handle = second_layout;
        second_record = second.record.handle;
        auto at = std::find_if(blocks_.begin(), blocks_.end(),
                               [&](const Block& b) { return same_name(b.name, "*Paper_Space"); });
        blocks_.insert(at + 1, std::move(second));
        rebuild_block_index();
    }

    // Owners before what they own; dictionary entries sorted by name, as AutoCAD does
    objects_.push_back(make_object(ObjectType::Dictionary, root, "0", dictionary({
        {"ACAD_GROUP", groups, false},
        {"ACAD_LAYOUT", layouts, false},
        {"ACAD_MLINESTYLE", mline_styles, false},
        {"ACAD_PLOTSETTINGS", plot_settings, false},
        {"ACAD_PLOTSTYLENAME", plot_styles, false},
    })));
    objects_.push_back(make_object(ObjectType::Dictionary, groups, root, dictionary({})));
    std::vector<DictionaryEntry> layout_entries = {{"Layout1", paper_layout, false}};
    if (!second_layout.empty()) layout_entries.push_back({"Layout2", second_layout, false});
    layout_entries.push_back({"Model", model_layout, false});
    objects_.push_back(make_object(ObjectType::Dictionary, layouts, root,
                                   dictionary(std::move(layout_entries))));
    objects_.push_back(make_object(ObjectType::Dictionary, mline_styles, root, dictionary({
        {"Standard", standard_mline, false},
    })));
    objects_.push_back(make_object(ObjectType::Dictionary, plot_settings, root, dictionary({})));
    DictionaryWithDefaultData styles;
    styles.dictionary = dictionary({{"Normal", normal, false}});
    styles.default_handle = normal;
    objects_.push_back(make_object(ObjectType::DictionaryWithDefault, plot_styles, root,
                                   std::move(styles)));
    // The Model layout's extents are 0 in AutoCAD's new drawing; Layout1, made current
    // once, has the empty 1E+20/-1E+20
    LayoutData model_data = layout("Model", 0, model_record);
    model_data.extents_min = {};
    model_data.extents_max = {};
    objects_.push_back(make_object(ObjectType::Layout, model_layout, layouts, std::move(model_data)));
    objects_.push_back(make_object(ObjectType::Layout, paper_layout, layouts,
                                   layout("Layout1", 1, paper_record)));
    if (!second_layout.empty()) {
        // Never made current: its extents are 0, as AutoCAD writes such a layout
        LayoutData second = layout("Layout2", 2, second_record);
        second.extents_min = {};
        second.extents_max = {};
        objects_.push_back(make_object(ObjectType::Layout, second_layout, layouts, std::move(second)));
    }
    objects_.push_back(make_object(ObjectType::MlineStyle, standard_mline, mline_styles,
                                   standard_mline_style()));
    objects_.push_back(make_object(ObjectType::Placeholder, normal, plot_styles,
                                   PlaceholderData{}));
}

// A drawing built in memory with R13's structure (the root dictionary with ACAD_GROUP and
// ACAD_MLINESTYLE, the two space blocks, no layouts) gets what 2000 adds to it: Model, Layout1 and
// Layout2 with their BLOCK_RECORDs, ACAD_LAYOUT, ACAD_PLOTSETTINGS and ACAD_PLOTSTYLENAME with
// Normal — the pieces it lacks, the others as they are (plan phase 7, B89: set_version() back and
// forth is the same drawing)
void Drawing::add_layout_structure() {
    Object* root_object = nullptr;
    for (auto& o : objects_) {
        if (o.type == ObjectType::Dictionary && o.owner == "0") { root_object = &o; break; }
    }
    auto find = [&](const char* name) -> Block* {
        for (auto& b : blocks_) {
            if (same_name(b.name, name)) return &b;
        }
        return nullptr;
    };
    if (!root_object || !find("*Model_Space") || !find("*Paper_Space")) return;
    scan_handles();

    for (const char* name : {"$FINGERPRINTGUID", "$VERSIONGUID"}) {
        const HeaderVar* h = find_header(name);
        if (!h || h->values.empty() || h->values[0].second.empty()) {
            set_header_string(name, 2, new_guid());
        }
    }

    // The dictionaries R13 made: 2000's duplicate record cloning flag, no R13 XDATA
    const std::string root_handle = root_object->handle;
    std::vector<std::string> owned = {root_handle};
    for (const char* name : {"ACAD_GROUP", "ACAD_MLINESTYLE"}) {
        for (const auto& entry : root_object->as_dictionary().entries) {
            if (entry.name == name) owned.push_back(entry.handle);
        }
    }
    for (const auto& handle : owned) {
        Object* d = find_object(handle);
        if (!d || d->type != ObjectType::Dictionary) continue;
        if (d->as_dictionary().cloning == 0) d->as_dictionary().cloning = 1;
        if (d->xdata.size() == 1 && d->xdata[0].app_name == "ACAD" && d->xdata[0].items.size() == 2 &&
            d->xdata[0].items[0].value == "TREAT_AS_HARD") {
            d->xdata.clear();
        }
    }

    const std::string layouts = named_dictionary("ACAD_LAYOUT");
    named_dictionary("ACAD_PLOTSETTINGS");
    bool has_styles = false;
    for (const auto& entry : find_object(root_handle)->as_dictionary().entries) {
        has_styles = has_styles || entry.name == "ACAD_PLOTSTYLENAME";
    }
    if (!has_styles) {
        std::string plot_styles = next_handle();
        std::string normal = next_handle();
        add_entry(find_object(root_handle)->as_dictionary(), "ACAD_PLOTSTYLENAME", plot_styles);
        DictionaryWithDefaultData styles;
        styles.dictionary = dictionary({{"Normal", normal, false}});
        styles.default_handle = normal;
        objects_.push_back(make_object(ObjectType::DictionaryWithDefault, plot_styles, root_handle,
                                       std::move(styles)));
        objects_.push_back(make_object(ObjectType::Placeholder, normal, plot_styles, PlaceholderData{}));
    }

    auto layout_named = [&](const char* name) {
        for (const auto* l : this->layouts()) {
            if (same_name(l->as_layout().name, name)) return true;
        }
        return false;
    };
    auto add = [&](const char* name, int tab_order, const std::string& record, bool empty_extents) {
        std::string handle = next_handle();
        if (Object* dict = find_object(layouts)) add_entry(dict->as_dictionary(), name, handle);
        LayoutData data = layout(name, tab_order, record);
        if (empty_extents) {
            data.extents_min = {};
            data.extents_max = {};
        }
        objects_.push_back(make_object(ObjectType::Layout, handle, layouts, std::move(data)));
        return handle;
    };
    // After the tabs a program's own layouts have (add_layout() in an R13 drawing)
    int tab = 0;
    for (const auto* l : this->layouts()) tab = std::max(tab, l->as_layout().tab_order);
    Block* model = find("*Model_Space");
    if (model->record.layout_handle.empty() && !layout_named("Model")) {
        std::string record = model->record.handle;
        std::string handle = add("Model", 0, record, true);
        find("*Model_Space")->record.layout_handle = handle;
    }
    Block* paper = find("*Paper_Space");
    if (paper->record.layout_handle.empty() && !layout_named("Layout1")) {
        std::string record = paper->record.handle;
        std::string handle = add("Layout1", ++tab, record, false);
        find("*Paper_Space")->record.layout_handle = handle;
    }
    if (!find("*Paper_Space0") && !layout_named("Layout2")) {
        Block second;
        second.name = "*Paper_Space0";
        second.record.handle = next_handle();
        std::string record = second.record.handle;
        auto at = std::find_if(blocks_.begin(), blocks_.end(),
                               [&](const Block& b) { return same_name(b.name, "*Paper_Space"); });
        blocks_.insert(at + 1, std::move(second));
        rebuild_block_index();
        std::string handle = add("Layout2", ++tab, record, true);
        find("*Paper_Space0")->record.layout_handle = handle;
    }
}

// Entries stay sorted by name, as AutoCAD 2000 and later write them; R13 and R14 list them in the
// order they were added (AutoCAD's R14 files: ACAD_GROUP, ACAD_MLINESTYLE, ACAD_IMAGE_DICT,
// ACAD_IMAGE_VARS)
static void add_entry(DictionaryData& dict, const std::string& name, const std::string& handle, bool sorted) {
    auto it = sorted ? std::find_if(dict.entries.begin(), dict.entries.end(),
                                    [&](const DictionaryEntry& e) { return e.name > name; })
                     : dict.entries.end();
    dict.entries.insert(it, {name, handle, false});
}

// The handle of the dictionary the root dictionary names `name` (ACAD_LAYOUT,
// ACAD_GROUP …). A drawing read from a file may lack it: it is made then, in the root.
std::string Drawing::named_dictionary(const std::string& name) {
    Object* root = nullptr;
    for (auto& o : objects_) {
        if (o.type == ObjectType::Dictionary && o.owner == "0") { root = &o; break; }
    }
    if (!root) return "0";
    for (const auto& entry : root->as_dictionary().entries) {
        if (entry.name == name) return entry.handle;
    }
    std::string handle = next_handle();
    std::string root_handle = root->handle;
    // R13 and R14: appended to the root, and no duplicate record cloning flag (AutoCAD's R14 files)
    const bool older = version_ < DxfVersion::R2000;
    add_entry(root->as_dictionary(), name, handle, !older);
    DictionaryData fresh = dictionary({});
    if (older) fresh.cloning = 0;
    objects_.push_back(make_object(ObjectType::Dictionary, handle, root_handle, std::move(fresh)));
    return handle;
}

// The dictionary the root dictionary names `name`; null when there is none (not made)
static const Object* root_dictionary_entry(const Drawing& dwg, const std::string& name) {
    for (const auto& o : dwg.objects()) {
        if (o.type != ObjectType::Dictionary || o.owner != "0") continue;
        for (const auto& entry : o.as_dictionary().entries) {
            if (entry.name != name) continue;
            const Object* d = dwg.find_object(entry.handle);
            return d && d->type == ObjectType::Dictionary ? d : nullptr;
        }
        return nullptr;
    }
    return nullptr;
}

// The DICTIONARYVAR XCLIPFRAME in AcDbVariableDictionary; null when there is none
static const Object* xclipframe_variable(const Drawing& dwg) {
    const Object* vars = root_dictionary_entry(dwg, "AcDbVariableDictionary");
    if (!vars) return nullptr;
    for (const auto& e : vars->as_dictionary().entries) {
        if (e.name != "XCLIPFRAME") continue;
        const Object* v = dwg.find_object(e.handle);
        return v && v->type == ObjectType::DictionaryVar ? v : nullptr;
    }
    return nullptr;
}

// The variable's value in the header (B43): a 2004 or 2007 file has a 2 (shown, not plotted)
// in the DICTIONARYVAR and 0 in the header; a 2000 file's header has no $XCLIPFRAME, its value
// is the variable's
void Drawing::read_xclipframe_variable(DxfVersion version) {
    if (version < DxfVersion::R2000 || version >= DxfVersion::R2010) return;
    const Object* v = xclipframe_variable(*this);
    if (!v) return;
    const std::string& value = v->as_dictionary_var().value;
    if (version == DxfVersion::R2000) {
        set_header_int("$XCLIPFRAME", 290, std::atoi(value.c_str()));
    } else if (std::atoi(value.c_str()) == 2) {
        HeaderVar* h = find_header("$XCLIPFRAME");
        if (h && !h->values.empty()) h->values[0].second = "2";
        else set_header_int("$XCLIPFRAME", 290, 2);
    }
}

// The DICTIONARYVAR XCLIPFRAME as AutoCAD has it for the header's value (B43): in 2004 and
// 2007 for a 2, in 2000 (no $XCLIPFRAME in its header) for every value but 0, in 2010 never
// (the header has the value) — made, given the value, or removed. A file's variable stays as
// it was read while it says the header's value (from_file)
void Drawing::sync_xclipframe_variable(bool from_file) {
    const HeaderVar* h = find_header("$XCLIPFRAME");
    int value = h && !h->values.empty() ? std::atoi(h->values[0].second.c_str()) : 0;
    std::string wanted;     // the variable's value; empty: none
    if (version_ == DxfVersion::R2000 && value != 0) wanted = std::to_string(value);
    if ((version_ == DxfVersion::R2004 || version_ == DxfVersion::R2007) && value == 2) wanted = "2";

    const Object* found = xclipframe_variable(*this);
    if (from_file) {
        int has = found ? std::atoi(found->as_dictionary_var().value.c_str()) : 0;
        bool agrees = version_ == DxfVersion::R2000 ? has == value : (has == 2) == (value == 2);
        if (agrees) return;
    }
    if (found) {
        Object* v = find_object(found->handle);
        if (!wanted.empty()) {
            v->as_dictionary_var().value = wanted;
            return;
        }
        // Gone with its entry in the dictionary
        std::string handle = v->handle;
        Object* vars = find_object(root_dictionary_entry(*this, "AcDbVariableDictionary")->handle);
        auto& entries = vars->as_dictionary().entries;
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [](const DictionaryEntry& e) { return e.name == "XCLIPFRAME"; }),
                      entries.end());
        objects_.erase(objects_.begin() + (find_object(handle) - objects_.data()));
        return;
    }
    if (wanted.empty()) return;
    std::string vars = named_dictionary("AcDbVariableDictionary");
    Object* dict = find_object(vars);
    if (!dict || dict->type != ObjectType::Dictionary) return;
    // As AutoCAD's 2000, 2004 and 2007 files have it (ACadSharp's sample_AC1015 … AC1021):
    // in the dictionary's name order, the dictionary its owner and reactor
    scan_handles();
    std::string var = next_handle();
    auto& entries = find_object(vars)->as_dictionary().entries;
    auto at = std::find_if(entries.begin(), entries.end(), [](const DictionaryEntry& e) { return e.name > "XCLIPFRAME"; });
    entries.insert(at, {"XCLIPFRAME", var, false});
    objects_.push_back(make_object(ObjectType::DictionaryVar, var, vars, DictionaryVarData{0, wanted}));
}

// As AutoCAD keeps it (LibreDWG's gh209_1, ACadSharp's geoloc): the model space BLOCK_RECORD's
// extension dictionary (hard-owning, owned by the record, no reactors) has the GEODATA under
// ACAD_GEOGRAPHICDATA; the GEODATA has the dictionary as owner and reactor
Object& Drawing::set_geodata(const GeoDataData& data) {
    ensure_r2000_structure();
    scan_handles();
    Block* model = find_block("*Model_Space");
    std::string& xdict = model->record.xdictionary;
    Object* dict = xdict.empty() ? nullptr : find_object(xdict);
    if (!dict || dict->type != ObjectType::Dictionary) {
        xdict = next_handle();
        DictionaryData d;
        d.hard_owner = true;
        Object o = make_object(ObjectType::Dictionary, xdict, model->record.handle, d);
        o.reactors.clear();
        objects_.push_back(std::move(o));
        dict = &objects_.back();
    }
    std::string dict_handle = dict->handle;
    for (const auto& e : dict->as_dictionary().entries) {
        if (e.name != "ACAD_GEOGRAPHICDATA") continue;
        if (Object* geo = find_object(e.handle); geo && geo->type == ObjectType::GeoData) {
            geo->as_geodata() = data;
            return *geo;
        }
    }
    std::string handle = next_handle();
    auto& entries = dict->as_dictionary().entries;
    auto at = std::find_if(entries.begin(), entries.end(), [](const DictionaryEntry& e) { return e.name > "ACAD_GEOGRAPHICDATA"; });
    entries.insert(at, {"ACAD_GEOGRAPHICDATA", handle, true});
    objects_.push_back(make_object(ObjectType::GeoData, handle, dict_handle, data));
    return objects_.back();
}

// The coordination model (B78): the definition NAVISWORKSMODELDEF under the file's name (without
// folder and extension) in ACAD_BIM_DEFINITIONS, one per path, and the entity pointing to it
Entity& Drawing::add_coordination_model(const std::string& path, Point3 min, Point3 max,
                                        const std::array<double, 16>& matrix, double unit_factor,
                                        const std::string& layer) {
    ensure_r2000_structure();
    scan_handles();
    std::string dict_handle = named_dictionary("ACAD_BIM_DEFINITIONS");
    std::string def_handle;
    for (const auto& o : objects_) {
        if (o.type == ObjectType::NavisworksModelDef && o.as_navisworks_model_def().path == path) def_handle = o.handle;
    }
    if (def_handle.empty()) {
        def_handle = next_handle();
        std::string name = path;
        size_t slash = name.find_last_of("/\\");
        if (slash != std::string::npos) name = name.substr(slash + 1);
        size_t dot = name.rfind('.');
        if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
        if (Object* dict = find_object(dict_handle)) {
            std::string unique = name;
            for (int n = 1; std::any_of(dict->as_dictionary().entries.begin(), dict->as_dictionary().entries.end(),
                                        [&](const DictionaryEntry& x) { return same_name(x.name, unique); });
                 ++n) {
                unique = name + "_" + std::to_string(n);
            }
            add_entry(dict->as_dictionary(), unique, def_handle);
        }
        NavisworksModelDefData def;
        def.path = path;
        def.min = min;
        def.max = max;
        objects_.push_back(make_object(ObjectType::NavisworksModelDef, def_handle, dict_handle, def));
    }
    Entity e;
    e.type = EntityType::CoordinationModel;
    e.props.layer = layer;
    CoordinationModelData d;
    d.definition_handle = def_handle;
    d.matrix = matrix;
    d.unit_factor = unit_factor;
    e.data = Boxed<CoordinationModelData>(d);
    entities_.push_back(std::move(e));
    return entities_.back();
}

// A render preset under its name in ACAD_RENDER_RAPIDRT_SETTINGS (as AutoCAD's file has them);
// one of that name is replaced
Object& Drawing::add_rapidrt_render_settings(const RapidRtRenderSettingsData& data) {
    ensure_r2000_structure();
    scan_handles();
    std::string dict_handle = named_dictionary("ACAD_RENDER_RAPIDRT_SETTINGS");
    Object* dict = find_object(dict_handle);
    if (dict) {
        for (const auto& entry : dict->as_dictionary().entries) {
            if (entry.name != data.name) continue;
            if (Object* o = find_object(entry.handle); o && o->type == ObjectType::RapidRtRenderSettings) {
                o->data = data;
                return *o;
            }
        }
    }
    std::string handle = next_handle();
    if (dict) add_entry(dict->as_dictionary(), data.name, handle);
    objects_.push_back(make_object(ObjectType::RapidRtRenderSettings, handle, dict_handle, data));
    return objects_.back();
}

// The IBL environment as the root dictionary's ACAD_BACKGROUND (B78); a background that was there
// is kept as its custom background (340)
Object& Drawing::set_rapidrt_render_environment(const RapidRtRenderEnvironmentData& data) {
    ensure_r2000_structure();
    scan_handles();
    Object* root = nullptr;
    for (auto& o : objects_) {
        if (o.type == ObjectType::Dictionary && o.owner == "0") { root = &o; break; }
    }
    std::string root_handle = root ? root->handle : "0";
    std::string previous;
    if (root) {
        for (const auto& entry : root->as_dictionary().entries) {
            if (entry.name == "ACAD_BACKGROUND") previous = entry.handle;
        }
    }
    if (!previous.empty()) {
        if (Object* o = find_object(previous); o && o->type == ObjectType::RapidRtRenderEnvironment) {
            o->data = data;
            return *o;
        }
    }
    RapidRtRenderEnvironmentData d = data;
    if (d.background_handle.empty() && !previous.empty()) d.background_handle = previous;
    std::string handle = next_handle();
    if (root) {
        auto& entries = root->as_dictionary().entries;
        auto it = std::find_if(entries.begin(), entries.end(), [](const DictionaryEntry& e) { return e.name == "ACAD_BACKGROUND"; });
        if (it != entries.end()) it->handle = handle;
        else add_entry(root->as_dictionary(), "ACAD_BACKGROUND", handle);
    }
    objects_.push_back(make_object(ObjectType::RapidRtRenderEnvironment, handle, root_handle, d));
    return objects_.back();
}

Object& Drawing::add_layout(const std::string& name) {
    ensure_r2000_structure();
    for (auto* l : layouts()) {
        if (same_name(l->as_layout().name, name)) return *l;
    }
    scan_handles();

    // *Paper_Space is the active layout's; the others are *Paper_Space0, 1, …
    int number = 0;
    while (find_block("*Paper_Space" + std::to_string(number))) ++number;
    int tab_order = 0;
    for (const auto* l : layouts()) tab_order = std::max(tab_order, l->as_layout().tab_order);

    std::string layout_handle = next_handle();
    Block block;
    block.name = "*Paper_Space" + std::to_string(number);
    block.record.handle = next_handle();
    block.record.layout_handle = layout_handle;
    std::string record = block.record.handle;
    blocks_.push_back(std::move(block));
    rebuild_block_index();

    std::string dict_handle = named_dictionary("ACAD_LAYOUT");
    if (Object* dict = find_object(dict_handle)) add_entry(dict->as_dictionary(), name, layout_handle);
    objects_.push_back(make_object(ObjectType::Layout, layout_handle, dict_handle,
                                   layout(name, tab_order + 1, record)));
    return objects_.back();
}

Object& Drawing::add_group(const std::string& name, const std::vector<Entity*>& members,
                           const std::string& description) {
    ensure_r2000_structure();
    scan_handles();

    GroupData data;
    data.description = description;
    for (Entity* e : members) {
        if (e->props.handle.empty()) e->props.handle = next_handle();
        data.entities.push_back(e->props.handle);
    }
    std::string handle = next_handle();
    std::string dict_handle = named_dictionary("ACAD_GROUP");
    if (Object* dict = find_object(dict_handle)) add_entry(dict->as_dictionary(), name, handle);
    objects_.push_back(make_object(ObjectType::Group, handle, dict_handle, std::move(data)));
    return objects_.back();
}

Entity& Drawing::add_mline(const std::vector<Point3>& vertices, bool closed, double scale,
                           int justification, const std::string& style, const std::string& layer) {
    ensure_r2000_structure();
    Entity e;
    e.type = EntityType::Mline;
    e.props.layer = layer;
    MlineData d;
    d.style = style;
    d.scale = scale;
    d.justification = justification;
    d.flags = 1 | (closed ? 2 : 0);
    for (const auto& p : vertices) {
        MlineVertex v;
        v.position = p;
        d.vertices.push_back(v);
    }
    std::vector<double> offsets;
    for (const auto& o : objects_) {
        if (o.type == ObjectType::MlineStyle && same_name(o.as_mline_style().name, style)) {
            d.style_handle = o.handle;
            for (const auto& el : o.as_mline_style().elements) offsets.push_back(el.offset);
            break;
        }
    }
    mline_geometry(d, offsets);
    e.data = std::move(d);
    entities_.push_back(std::move(e));
    return entities_.back();
}

// IMAGEDEF in ACAD_IMAGE_DICT (one per file, named by the file's name without its
// folder and extension, as AutoCAD names it), RASTERVARIABLES as ACAD_IMAGE_VARS of the
// root dictionary, and the IMAGEDEF_REACTOR the image owns — linked both ways
Entity& Drawing::add_image(const std::string& file_name, Point3 insertion_point, Point2 size,
                           double width, const std::string& layer) {
    ensure_r2000_structure();
    scan_handles();
    Object* root = nullptr;
    for (auto& o : objects_) {
        if (o.type == ObjectType::Dictionary && o.owner == "0") { root = &o; break; }
    }
    std::string root_handle = root ? root->handle : "0";
    bool has_variables = false;
    if (root) {
        for (const auto& entry : root->as_dictionary().entries) has_variables = has_variables || entry.name == "ACAD_IMAGE_VARS";
    }
    // R14 (the image types are R14's; R13 refuses them when written, 7.15): ACAD_IMAGE_DICT is
    // appended to the root dictionary before ACAD_IMAGE_VARS, as AutoCAD's R14 files list them,
    // and the entries are not sorted
    const bool older = version_ < DxfVersion::R2000;
    std::string dict_handle;
    if (older) {
        dict_handle = named_dictionary("ACAD_IMAGE_DICT");
        root = nullptr;
        for (auto& o : objects_) {
            if (o.type == ObjectType::Dictionary && o.owner == "0") { root = &o; break; }
        }
    }
    if (root && !has_variables) {
        std::string handle = next_handle();
        add_entry(root->as_dictionary(), "ACAD_IMAGE_VARS", handle, !older);
        objects_.push_back(make_object(ObjectType::RasterVariables, handle, root_handle, RasterVariablesData{}));
    }

    if (!older) dict_handle = named_dictionary("ACAD_IMAGE_DICT");
    std::string def_handle;
    for (const auto& o : objects_) {
        if (o.type == ObjectType::ImageDef && o.as_image_def().file_name == file_name) def_handle = o.handle;
    }
    if (def_handle.empty()) {
        def_handle = next_handle();
        std::string name = file_name;
        size_t slash = name.find_last_of("/\\");
        if (slash != std::string::npos) name = name.substr(slash + 1);
        size_t dot = name.rfind('.');
        if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
        if (older) name = older_symbol_name(name);
        if (Object* dict = find_object(dict_handle)) {
            std::string unique = name;
            for (int n = 1; std::any_of(dict->as_dictionary().entries.begin(), dict->as_dictionary().entries.end(),
                                        [&](const DictionaryEntry& x) { return same_name(x.name, unique); });
                 ++n) {
                unique = name + "_" + std::to_string(n);
            }
            add_entry(dict->as_dictionary(), unique, def_handle, !older);
        }
        ImageDefData def;
        def.file_name = file_name;
        def.size = size;
        objects_.push_back(make_object(ObjectType::ImageDef, def_handle, dict_handle, def));
    }

    Entity e;
    e.type = EntityType::Image;
    e.props.layer = layer;
    e.props.handle = next_handle();
    std::string reactor = next_handle();
    ImageData d;
    double pixel = size.x > 0.0 ? width / size.x : 1.0;
    d.insertion_point = insertion_point;
    d.u_vector = {pixel, 0.0, 0.0};
    d.v_vector = {0.0, pixel, 0.0};
    d.size = size;
    d.image_def_handle = def_handle;
    d.reactor_handle = reactor;
    d.clip_boundary = {{-0.5, -0.5}, {size.x - 0.5, size.y - 0.5}};
    e.data = d;

    // The reactor has no reactors of its own; the definition has the reactor as one
    Object r = make_object(ObjectType::ImageDefReactor, reactor, e.props.handle,
                           ImageDefReactorData{2, e.props.handle});
    r.reactors.clear();
    objects_.push_back(std::move(r));
    if (Object* def = find_object(def_handle)) {
        // R14: the IMAGEDEF_REACTORs first and ACAD_IMAGE_DICT last (the IMAGEDEF of both of
        // AutoCAD's R14 files with images: `330 <reactor>`, `330 <dictionary>`); the R14
        // reference lists the dictionary first. 2000 and later: the dictionary first.
        auto at = def->reactors.end();
        if (older && !def->reactors.empty() && def->reactors.back() == dict_handle) at = def->reactors.end() - 1;
        def->reactors.insert(at, reactor);
    }

    entities_.push_back(std::move(e));
    return entities_.back();
}

// As AutoCAD makes one (ACadSharp's sample: a frame the square of the polygon's larger side at
// its box's lower left, one pixel of that size, the boundary in -0.5 … 0.5 with y down, closed
// by repeating the first point; no IMAGEDEF or reactor, 340 and 360 0)
Entity& Drawing::add_wipeout(const std::vector<Point3>& polygon, const std::string& layer) {
    ensure_r2000_structure();
    scan_handles();
    Object* root = nullptr;
    for (auto& o : objects_) {
        if (o.type == ObjectType::Dictionary && o.owner == "0") { root = &o; break; }
    }
    bool has_variables = false;
    if (root) {
        for (const auto& entry : root->as_dictionary().entries) has_variables = has_variables || entry.name == "ACAD_WIPEOUT_VARS";
    }
    if (root && !has_variables) {
        std::string handle = next_handle();
        add_entry(root->as_dictionary(), "ACAD_WIPEOUT_VARS", handle);
        objects_.push_back(make_object(ObjectType::WipeoutVariables, handle, root->handle, WipeoutVariablesData{}));
    }

    double min_x = 0.0, min_y = 0.0, max_x = 0.0, max_y = 0.0;
    for (size_t i = 0; i < polygon.size(); ++i) {
        const Point3& p = polygon[i];
        if (i == 0 || p.x < min_x) min_x = p.x;
        if (i == 0 || p.y < min_y) min_y = p.y;
        if (i == 0 || p.x > max_x) max_x = p.x;
        if (i == 0 || p.y > max_y) max_y = p.y;
    }
    double side = std::max(max_x - min_x, max_y - min_y);
    if (!(side > 0.0)) side = 1.0;
    Entity e;
    e.type = EntityType::Wipeout;
    e.props.layer = layer;
    e.props.handle = next_handle();
    ImageData d;
    d.insertion_point = {min_x, min_y, polygon.empty() ? 0.0 : polygon[0].z};
    d.u_vector = {side, 0.0, 0.0};
    d.v_vector = {0.0, side, 0.0};
    d.size = {1.0, 1.0};
    d.image_def_handle = "0";
    d.reactor_handle = "0";
    d.clipping = true;
    d.clip_boundary_type = 2;
    for (const auto& p : polygon) d.clip_boundary.push_back({(p.x - min_x) / side - 0.5, 0.5 - (p.y - min_y) / side});
    if (!d.clip_boundary.empty()) {
        const Point2& a = d.clip_boundary.front();
        const Point2& b = d.clip_boundary.back();
        if (polygon.size() < 2 || a.x != b.x || a.y != b.y) d.clip_boundary.push_back(a);
    }
    e.data = d;
    entities_.push_back(std::move(e));
    return entities_.back();
}

// The edges as AutoCAD lists them (every MESH of its files): face by face, each face's edges in
// its order, each edge once, the smaller index first
Entity& Drawing::add_mesh(const std::vector<Point3>& vertices, const std::vector<std::vector<int>>& faces, int level,
                          const std::string& layer) {
    MeshData d;
    d.level = level;
    d.vertices = vertices;
    d.faces = faces;
    std::set<std::pair<int, int>> seen;
    for (const auto& f : faces) {
        for (size_t k = 0; k < f.size(); ++k) {
            int a = f[k], b = f[(k + 1) % f.size()];
            std::pair<int, int> edge = {std::min(a, b), std::max(a, b)};
            if (seen.insert(edge).second) d.edges.push_back({edge.first, edge.second});
        }
    }
    d.creases.assign(d.edges.size(), 0.0);
    Entity e;
    e.type = EntityType::Mesh;
    e.props.layer = layer;
    e.data = std::move(d);
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_surface(EntityType type, SurfaceData data, const std::string& layer) {
    Entity e;
    switch (type) {
        case EntityType::ExtrudedSurface: case EntityType::LoftedSurface: case EntityType::RevolvedSurface:
        case EntityType::SweptSurface: case EntityType::PlaneSurface: case EntityType::NurbsSurface:
            e.type = type;
            break;
        default:
            e.type = EntityType::Surface;
            break;
    }
    e.props.layer = layer;
    e.data = std::move(data);
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_acis(EntityType type, AcisData data, const std::string& layer) {
    Entity e;
    e.type = (type == EntityType::Region || type == EntityType::Body) ? type : EntityType::Solid3D;
    e.props.layer = layer;
    e.data = std::move(data);
    entities_.push_back(std::move(e));
    return entities_.back();
}

} // namespace hdxf
