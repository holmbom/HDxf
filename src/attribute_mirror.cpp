#include "attribute_mirror.hpp"
#include "group_io.hpp"
#include <cmath>

namespace hdxf {
namespace internal {

namespace {

Point3 unit(Point3 v, Point3 fallback) {
    double l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return l > 0.0 ? Point3{v.x / l, v.y / l, v.z / l} : fallback;
}

// The text plane: x along the direction, y the normal times x
void plane_of(const MultilineAttribute& ml, const Point3& extrusion, Point3& dir, Point3& up) {
    Point3 n = unit(extrusion, {0.0, 0.0, 1.0});
    dir = unit(ml.text.x_axis_direction, {1.0, 0.0, 0.0});
    up = {n.y * dir.z - n.z * dir.y, n.z * dir.x - n.x * dir.z, n.x * dir.y - n.y * dir.x};
}

Point3 along(const Point3& origin, const Point3& dir, double x, const Point3& up, double y) {
    return {origin.x + dir.x * x + up.x * y, origin.y + dir.y * x + up.y * y, origin.z + dir.z * x + up.z * y};
}

std::string format(double v) { return format_double(v, NumberStyle::AutoCad); }

} // namespace

std::vector<std::string> text_in_parts(const std::string& text) {
    std::vector<std::string> parts;
    size_t from = 0, characters = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char b = static_cast<unsigned char>(text[i]);
        if ((b & 0xC0) == 0x80) continue;           // a continuation byte
        if (characters == 250) {
            parts.push_back(text.substr(from, i - from));
            from = i;
            characters = 0;
        }
        ++characters;
    }
    parts.push_back(text.substr(from));
    return parts;
}

bool attributes_by_runs(const InsertData& insert) {
    return insert.rotation != 0.0 || insert.x_scale != 1.0 || insert.y_scale != 1.0 || insert.z_scale != 1.0;
}

std::vector<AttributeLine> attribute_lines(const MultilineAttribute& ml, const Point3& extrusion, TextLayout& layout,
                                           bool by_runs) {
    std::vector<AttributeLine> out;
    Point3 dir, up;
    plane_of(ml, extrusion, dir, up);
    const Point3& ins = ml.text.insertion_point;
    if (by_runs) {
        // The runs of the laid out text, each at its own pen (the stacks' bars are no text)
        MTextSize runs = layout.mtext(ml.text, true);
        if (runs.font) {
            for (const auto& r : runs.runs) {
                if (r.bar || r.code_text.empty()) continue;
                AttributeLine line;
                line.pen = ecs::wcs_to_ecs(along(ins, dir, r.x, up, r.y), extrusion);
                line.height = r.height;
                line.text = r.code_text;
                out.push_back(std::move(line));
            }
            if (!out.empty()) return out;
        }
    }
    MTextSize size = layout.mtext(ml.text);
    if (size.font && !size.text_lines.empty()) {
        for (const auto& l : size.text_lines) {
            AttributeLine line;
            line.pen = ecs::wcs_to_ecs(along(ins, dir, l.x, up, l.baseline), extrusion);
            line.height = l.height;
            line.text = l.text;
            out.push_back(std::move(line));
        }
        return out;
    }
    // No font: the paragraphs, one below the other
    std::vector<std::string> paragraphs = layout.mtext_paragraphs(ml.text.content, ml.text.style);
    if (paragraphs.empty()) paragraphs.push_back(std::string());
    double h = ml.text.height;
    for (size_t k = 0; k < paragraphs.size(); ++k) {
        AttributeLine line;
        line.pen = ecs::wcs_to_ecs(along(ins, dir, 0.0, up, -h - static_cast<double>(k) * h * 5.0 / 3.0), extrusion);
        line.height = h;
        line.text = paragraphs[k];
        out.push_back(std::move(line));
    }
    return out;
}

AttributeMirror attribute_mirror(const MultilineAttribute& ml, const std::string& tag, bool definition,
                                 DxfVersion version, const Point3& extrusion, TextLayout& layout,
                                 double width_factor, double oblique, int generation_flags, bool by_runs) {
    AttributeMirror m;
    Point3 dir, up;
    plane_of(ml, extrusion, dir, up);
    const Point3& ins = ml.text.insertion_point;
    std::vector<AttributeLine> lines = attribute_lines(ml, extrusion, layout, by_runs);
    std::string first = lines.empty() ? std::string() : lines[0].text;
    m.height = ml.text.height;
    m.style = ml.text.style;
    // The direction as an angle in the OCS of the attribute
    Point3 d = ecs::wcs_to_ecs({dir.x, dir.y, dir.z}, extrusion);
    Point3 o = ecs::wcs_to_ecs({0.0, 0.0, 0.0}, extrusion);
    m.rotation = std::atan2(d.y - o.y, d.x - o.x) * 180.0 / 3.14159265358979323846;
    if (std::fabs(m.rotation) < 1e-12) m.rotation = 0.0;
    m.tag = tag;
    if (definition || version >= DxfVersion::R2018) {
        int a = ml.text.attachment >= 1 && ml.text.attachment <= 9 ? ml.text.attachment : 1;
        m.h_justify = (a - 1) % 3;
        m.v_justify = 3 - (a - 1) / 3;
        m.alignment_point = ecs::wcs_to_ecs(ins, extrusion);
        m.value = version >= DxfVersion::R2018 ? std::string() : first;
        // An attribute definition up to 2013 is read back by AutoCAD from this TEXT, not from its
        // XRECORD: the MText's insertion point is the text's pen plus a third of the height for
        // the bottom row (its top row: the height, its middle: half — the justification's own
        // offsets, so the alignment point is the insertion point there). Measured with AutoCAD's
        // core on definitions of every attachment, heights 2 and 4: the bottom row ends 0.62938 times
        // the height higher in Arial — TEXT's bottom offset (0.296) and a third.
        if (definition && version < DxfVersion::R2018 && m.v_justify == 1) {
            TextLine probe;
            probe.style = m.style;
            probe.height = m.height;
            probe.width_factor = width_factor;
            probe.oblique_angle = oblique;
            probe.generation_flags = generation_flags;
            probe.h_justify = m.h_justify;
            probe.v_justify = 1;
            TextPen offset = layout.text_pen(probe);
            if (offset.font) {
                double lift = offset.pen.y + m.height / 3.0;
                m.alignment_point = ecs::wcs_to_ecs(along(ins, dir, 0.0, up, -lift), extrusion);
            }
        }
        // The pen of the text shown (its value, or the tag), justified at the alignment point
        TextLine t;
        t.style = m.style;
        t.content = m.value.empty() ? tag : m.value;
        t.height = m.height;
        t.width_factor = width_factor;
        t.oblique_angle = oblique;
        t.rotation = m.rotation;
        t.generation_flags = generation_flags;
        t.h_justify = m.h_justify;
        t.v_justify = m.v_justify;
        t.position = m.alignment_point;
        t.alignment_point = m.alignment_point;
        TextPen pen = layout.text_pen(t);
        // (no font: the insertion point less the height, the first line's top, as for the top-left)
        m.start_point = pen.font ? pen.pen : ecs::wcs_to_ecs(along(ins, dir, 0.0, up, -ml.text.height), extrusion);
    } else {
        m.tag = tag + "_001";
        m.start_point = lines.empty() ? ecs::wcs_to_ecs(ins, extrusion) : lines[0].pen;
        m.alignment_point = m.start_point;
        m.value = first;
    }
    return m;
}

std::vector<DxfGroup> mlatt_xrecord_data(const MultilineAttribute& ml, const std::string& tag) {
    std::vector<DxfGroup> g;
    auto add = [&](int code, const std::string& v) { g.push_back({code, v}); };
    add(70, std::to_string(ml.type));
    add(70, ml.really_locked ? "1" : "0");
    add(70, std::to_string(ml.secondary.size()));
    for (const auto& h : ml.secondary) add(340, h);
    add(40, format(ml.annotation_scale));
    add(2, ml.tag.empty() ? tag : ml.tag);
    add(1, "Embedded Object");
    const MTextData& t = ml.text;
    add(10, format(t.insertion_point.x));
    add(20, format(t.insertion_point.y));
    add(30, format(t.insertion_point.z));
    add(40, format(t.height));
    add(41, format(t.reference_width));
    add(46, format(t.defined_height));
    add(71, std::to_string(t.attachment));
    add(72, std::to_string(t.drawing_direction));
    std::vector<std::string> parts = text_in_parts(t.content);
    for (size_t i = 0; i + 1 < parts.size(); ++i) add(3, parts[i]);
    add(1, parts.back());
    if (t.style != "Standard" && !t.style.empty()) add(7, t.style);
    const Point3& x = t.x_axis_direction;
    if (!(x.x == 1.0 && x.y == 0.0 && x.z == 0.0)) {
        add(11, format(x.x));
        add(21, format(x.y));
        add(31, format(x.z));
    }
    add(73, std::to_string(t.line_spacing_style));
    add(44, format(t.line_spacing_factor));
    const MTextBackground& bg = t.background;
    if (bg.flags != 0) {
        add(90, std::to_string(bg.flags));
        add(63, std::to_string(bg.color.aci));
        if (bg.color.has_rgb()) add(421, std::to_string(bg.color.rgb));
        if (!bg.color.name.empty()) add(431, bg.color.name);
        add(45, format(bg.scale));
        add(441, std::to_string(bg.transparency.raw));
    }
    return g;
}

std::vector<DxfGroup> matt_height_xrecord_data(const MultilineAttribute& ml) {
    return {{102, "ACAD_MATT_HEIGHT"}, {140, format(ml.text.height)}};
}

} // namespace internal
} // namespace hdxf
