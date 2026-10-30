#include "older_tables.hpp"
#include "dxf_writer.hpp"

namespace hdxf {
namespace internal {

namespace {

// A name's length in characters (the bytes of UTF-8 that are not continuation bytes)
size_t characters_in(const std::string& name) {
    size_t n = 0;
    for (unsigned char c : name) {
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

constexpr size_t name_limit = 31;
constexpr size_t mline_style_limit = 32;

bool same(const Point3& a, const Point3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

const Point3 x_axis = {1.0, 0.0, 0.0};
const Point3 y_axis = {0.0, 1.0, 0.0};

std::string too_long(const std::string& what, const std::string& name, size_t limit) {
    return what + " name \"" + name + "\" is longer than " + std::to_string(limit) +
           " characters, which DXF R13 and R14 do not allow";
}

// The first byte of the character at `i` of a UTF-8 string and the whole character
std::string character_at(const std::string& s, size_t i) {
    size_t n = 1;
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c >= 0xF0) n = 4;
    else if (c >= 0xE0) n = 3;
    else if (c >= 0xC0) n = 2;
    return s.substr(i, n);
}

bool allowed_in_older_name(unsigned char c, bool xref_dependent) {
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= 'a' && c <= 'z') return true;
    if (c >= '0' && c <= '9') return true;
    if (c == '$' || c == '_' || c == '-') return true;
    return xref_dependent && c == '|';
}

// The records of `was` (the file as read) that have `name`, nullptr for none
template <typename T>
const T* find_named(const std::vector<T>* was, const std::string& name) {
    if (!was) return nullptr;
    for (const auto& x : *was) {
        if (names_equal(x.name, name)) return &x;
    }
    return nullptr;
}

// The first record of `now` with a fault `check` names that the file did not have: a record of the file
// with the same name, the same fault and the same values (`same_values`) is written from its preserved
// record, whatever its fault (a file AutoCAD saved as R14 from a newer drawing may have such a record)
template <typename T, typename Check, typename Same>
std::string first_new_error(const std::vector<T>& now, const std::vector<T>* was, Check check, Same same_values) {
    for (const auto& x : now) {
        std::string error = check(x);
        if (error.empty()) continue;
        if (const T* before = find_named(was, x.name)) {
            if (check(*before) == error && same_values(x, *before)) continue;
        }
        return error;
    }
    return {};
}

template <typename T, typename Check>
std::string first_new_error(const std::vector<T>& now, const std::vector<T>* was, Check check) {
    return first_new_error(now, was, check, [](const T&, const T&) { return true; });
}

} // namespace

std::string older_symbol_name_error(const std::string& what, const std::string& name, bool xref_dependent) {
    if (characters_in(name) > name_limit) return too_long(what, name, name_limit);
    size_t start = !name.empty() && name[0] == '*' ? 1 : 0;
    for (size_t i = start; i < name.size();) {
        std::string c = character_at(name, i);
        if (c.size() > 1 || !allowed_in_older_name(static_cast<unsigned char>(c[0]), xref_dependent)) {
            return what + " name \"" + name + "\" has the character \"" + c +
                   "\", which DXF R13 and R14 do not allow (a name has at most 31 characters: the letters A to Z, "
                   "the digits, $, _ and -)";
        }
        i += c.size();
    }
    return {};
}

std::string older_mline_style_name_error(const std::string& name) {
    if (characters_in(name) > mline_style_limit) return too_long("MLINE style", name, mline_style_limit);
    return {};
}

std::string older_tables_error(const Drawing& dwg, const Drawing* original) {
    // The symbol names, the tables in file order and then the blocks; a record's name is checked as it is
    // spelled (xref-dependent ones, 70 bit 16, may have "|")
    auto names = [&](const auto& list, const auto* was, const char* what) {
        return first_new_error(list, was, [&](const auto& x) {
            bool xref = false;
            if constexpr (requires { x.flags; }) xref = (x.flags & 16) != 0;
            return older_symbol_name_error(what, x.name, xref);
        });
    };
    for (const std::string& e :
         {names(dwg.layers(), original ? &original->layers() : nullptr, "Layer"),
          names(dwg.linetypes(), original ? &original->linetypes() : nullptr, "Linetype"),
          names(dwg.text_styles(), original ? &original->text_styles() : nullptr, "Text style"),
          names(dwg.views(), original ? &original->views() : nullptr, "View"),
          names(dwg.ucs_list(), original ? &original->ucs_list() : nullptr, "UCS"),
          names(dwg.app_ids(), original ? &original->app_ids() : nullptr, "Application"),
          names(dwg.dim_styles(), original ? &original->dim_styles() : nullptr, "Dimension style"),
          names(dwg.vports(), original ? &original->vports() : nullptr, "Viewport configuration"),
          names(dwg.blocks(), original ? &original->blocks() : nullptr, "Block")}) {
        if (!e.empty()) return e;
    }

    // LAYER has 62 and 6 and the flags; the 2000 groups for plotting have no place in R13 and R14, and
    // the transparency is XDATA that the older record does not have either (7.6)
    {
        std::string error = first_new_error(
            dwg.layers(), original ? &original->layers() : nullptr,
            [](const Layer& l) -> std::string {
                if (l.lineweight != -3) return "Lineweight requires DXF 2000 or later";
                if (!l.plot_style_handle.empty()) return "Plot style requires DXF 2000 or later";
                if (!l.plot) return "Layer \"" + l.name + "\": the plot flag requires DXF 2000 or later";
                if (!l.transparency.is_by_layer()) return "Layer \"" + l.name + "\": transparency requires DXF 2000 or later";
                return {};
            },
            [](const Layer& a, const Layer& b) {
                return a.lineweight == b.lineweight && a.plot_style_handle == b.plot_style_handle &&
                       a.plot == b.plot && a.transparency.raw == b.transparency.raw;
            });
        if (!error.empty()) return error;
    }

    // The UCS a view, a viewport or a UCS record saves is 2000's: 110–112, 79, 146, 345, 346, 281
    {
        std::string error = first_new_error(
            dwg.views(), original ? &original->views() : nullptr, [](const View& v) -> std::string {
                if (v.has_ucs || v.render_mode != 0 || !v.ucs.empty() || !v.base_ucs.empty()) {
                    return "View \"" + v.name + "\": its UCS and render mode require DXF 2000 or later";
                }
                return {};
            });
        if (!error.empty()) return error;
        error = first_new_error(
            dwg.vports(), original ? &original->vports() : nullptr, [](const Vport& vp) -> std::string {
                bool wcs = same(vp.ucs_origin, Point3{}) && same(vp.ucs_x_axis, x_axis) && same(vp.ucs_y_axis, y_axis);
                if (!wcs || vp.ucs_ortho_type != 0 || vp.elevation != 0.0 || vp.render_mode != 0 || !vp.ucs.empty() ||
                    !vp.base_ucs.empty()) {
                    return "Viewport configuration \"" + vp.name + "\": its UCS and render mode require DXF 2000 or later";
                }
                return {};
            });
        if (!error.empty()) return error;
        error = first_new_error(
            dwg.ucs_list(), original ? &original->ucs_list() : nullptr, [](const Ucs& u) -> std::string {
                if (u.ortho_type != 0 || u.elevation != 0.0 || !u.base_ucs.empty() || !u.orthographic.empty()) {
                    return "UCS \"" + u.name + "\": its orthographic data and base UCS require DXF 2000 or later";
                }
                return {};
            });
        if (!error.empty()) return error;
        error = first_new_error(
            dwg.blocks(), original ? &original->blocks() : nullptr, [](const Block& b) -> std::string {
                if (!b.record.preview.empty()) return "Block \"" + b.name + "\": its preview image requires DXF 2000 or later";
                return {};
            });
        if (!error.empty()) return error;
    }
    return {};
}

} // namespace internal
} // namespace hdxf
