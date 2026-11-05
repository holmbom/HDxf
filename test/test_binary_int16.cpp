#include "test_framework.hpp"
#include "type_version_matrix.hpp"
#include "hdxf.hpp"
#include "io/group_io.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// 16-bit groups beyond -32768 ... 32767. AutoCAD writes the low 16 bits as a signed number, in ASCII and in binary alike
// (a polyface with 40 000 vertices has 71 = -25536 and face indices -25536, -25537, -25538; a table of 40 001 layers has
// 70 = -25535): a value beyond the range is cut so and is not an error. The writer tells it in warnings(); the count of
// a table is cut without a word. A value held as text in an ASCII file stays the text it was.
// ---------------------------------------------------------------------------

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::internal::BinaryCodeWidth;
using hdxf::internal::GroupCode;
using hdxf::internal::GroupReader;
using hdxf::internal::GroupWriter;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

namespace {

const DxfVersion kVersions[] = {DxfVersion::R12,   DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007,
                                DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018};
const DxfFormat kFormats[] = {DxfFormat::Ascii, DxfFormat::Binary};

const char* label(DxfVersion v) { return hdxf_test::version_label(v); }
const char* label(DxfFormat f) { return hdxf_test::format_label(f); }

int cut16(long long value) { return static_cast<int16_t>(static_cast<uint16_t>(static_cast<unsigned long long>(value) & 0xFFFF)); }

std::string printf_width(int width, long long value) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%*lld", width, value);
    return buf;
}

// AutoCAD's file of this kind, or an empty path when it is not here (doc/facit/large is not in the repository)
std::filesystem::path facit_file(DxfVersion version, const char* kind, DxfFormat format) {
    const char* tag = "";
    switch (version) {
        case DxfVersion::R12:   tag = "r12"; break;
        case DxfVersion::R2000: tag = "2000"; break;
        case DxfVersion::R2004: tag = "2004"; break;
        case DxfVersion::R2007: tag = "2007"; break;
        case DxfVersion::R2010: tag = "2010"; break;
        case DxfVersion::R2013: tag = "2013"; break;
        case DxfVersion::R2018: tag = "2018"; break;
        default: return {};
    }
    std::filesystem::path path = hdxf::path_from_utf8(HDXF_SOURCE_DIR) / "doc" / "facit" / "large" /
                                 (std::string("acad_") + tag + "_dxf_int16_" + kind + (format == DxfFormat::Binary ? "_bin" : "") + ".dxf");
    return std::filesystem::exists(path) ? path : std::filesystem::path();
}

constexpr int kPolyfaceVertices = 40000;

// A polyface mesh of 40 000 vertices and one face that refers to the last three
hdxf::Entity make_polyface(int count) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Polyline;
    e.props.layer = "0";
    hdxf::PolylineData d;
    d.flags = 64;
    d.m_count = count;
    d.n_count = 1;
    for (int i = 0; i < count; ++i) {
        hdxf::Vertex v;
        v.position = {static_cast<double>(i), 0.0, 0.0};
        v.flags = 192;
        d.vertices.push_back(v);
    }
    hdxf::Vertex face;
    face.flags = 128;
    face.face[0] = count;
    face.face[1] = count - 1;
    face.face[2] = count - 2;
    d.vertices.push_back(face);
    e.data = d;
    return e;
}

const hdxf::PolylineData* polyline_of(const hdxf::Drawing& d) {
    for (const hdxf::Entity& e : d.entities()) {
        if (e.type == hdxf::EntityType::Polyline) return &e.as_polyline();
    }
    return nullptr;
}

hdxf::Drawing make_layers(DxfVersion version, int count) {
    hdxf::Drawing d(version);
    hdxf::Layer zero;
    zero.name = "0";
    d.layers().push_back(std::move(zero));
    for (int i = 0; i < count; ++i) {
        hdxf::Layer layer;
        layer.name = "L" + std::to_string(i);
        d.layers().push_back(std::move(layer));
    }
    hdxf::Entity e;
    e.type = hdxf::EntityType::Line;
    e.props.layer = "L" + std::to_string(count - 1);
    hdxf::LineData line;
    line.end = {1.0, 1.0, 0.0};
    e.data = line;
    d.add_entity(e);
    return d;
}

// The count in TABLE `name`'s group 70, as the file has it
bool table_count(const std::vector<uint8_t>& bytes, const std::string& name, int& count) {
    std::istringstream in(std::string(bytes.begin(), bytes.end()), std::ios::binary);
    GroupReader reader(in);
    reader.detect_format();
    GroupCode gc;
    int state = 0;
    while (reader.read(gc)) {
        if (state == 0 && gc.code == 0 && gc.value == "TABLE") state = 1;
        else if (state == 1 && gc.code == 2) state = gc.value == name ? 2 : 0;
        else if (state == 2 && gc.code == 70) {
            count = std::atoi(gc.value.c_str());
            return true;
        } else if (state == 2 && gc.code == 0) {
            return false;
        }
    }
    return false;
}

bool has_warning(const hdxf::Drawing& d, const std::string& text) {
    for (const std::string& w : d.warnings()) {
        if (w == text) return true;
    }
    return false;
}

bool any_warning_with(const hdxf::Drawing& d, const std::string& text) {
    for (const std::string& w : d.warnings()) {
        if (w.find(text) != std::string::npos) return true;
    }
    return false;
}

// Groups written by `fill`, as the file format holds them
struct Written {
    std::string bytes;
    std::vector<std::string> warnings;
};

Written write_groups(DxfFormat format, BinaryCodeWidth width, const std::function<void(GroupWriter&)>& fill) {
    std::ostringstream out(std::ios::binary);
    GroupWriter writer(out, format, width);
    if (format == DxfFormat::Binary) {
        writer.write_sentinel();
        writer.write_string(0, "SECTION");
    }
    fill(writer);
    Written w;
    w.bytes = out.str();
    w.warnings = writer.wrapped_value_warnings();
    return w;
}

// The (code, value) pairs of a binary stream past its first group
std::vector<std::pair<int, std::string>> read_binary_groups(const std::string& bytes) {
    std::istringstream in(bytes, std::ios::binary);
    GroupReader reader(in);
    reader.detect_format();
    std::vector<std::pair<int, std::string>> out;
    GroupCode gc;
    bool first = true;
    while (reader.read(gc)) {
        if (first) { first = false; continue; }
        out.push_back({gc.code, gc.value});
    }
    return out;
}

std::vector<uint8_t> bytes_of(const std::string& text) { return std::vector<uint8_t>(text.begin(), text.end()); }

} // namespace

TEST(beyond_16_bits_written_as_autocad) {
    // The writer: every way to write a 16-bit group cuts a value beyond the range, in both formats and both code widths
    struct Case {
        int code;
        std::function<void(GroupWriter&)> write;
        long long value;        // what was asked for
        bool text_kept;         // ASCII keeps the text of a value held as text
        const char* text;       // that text
    };
    const Case cases[] = {
        {71, [](GroupWriter& w) { w.write_int16(71, 40000); }, 40000, false, ""},
        {71, [](GroupWriter& w) { w.write_int16(71, 32767); }, 32767, false, ""},
        {71, [](GroupWriter& w) { w.write_int16(71, -32768); }, -32768, false, ""},
        {71, [](GroupWriter& w) { w.write_int16(71, 32768); }, 32768, false, ""},
        {71, [](GroupWriter& w) { w.write_int16(71, 65535); }, 65535, false, ""},
        {71, [](GroupWriter& w) { w.write_int16(71, 65536); }, 65536, false, ""},
        {71, [](GroupWriter& w) { w.write_int16(71, -32769); }, -32769, false, ""},
        {71, [](GroupWriter& w) { w.write_int16(71, 100000); }, 100000, false, ""},
        {72, [](GroupWriter& w) { w.write_int(72, 70000); }, 70000, false, ""},
        {72, [](GroupWriter& w) { w.write_int(72, -70000); }, -70000, false, ""},
        {73, [](GroupWriter& w) { w.write_typed(73, "40000"); }, 40000, true, "40000"},
        {73, [](GroupWriter& w) { w.write_typed(73, "-40000"); }, -40000, true, "-40000"},
        {73, [](GroupWriter& w) { w.write_typed(73, "7"); }, 7, false, ""},
        {74, [](GroupWriter& w) { w.write_raw(74, "     40000", false); }, 40000, true, "     40000"},
        {74, [](GroupWriter& w) { w.write_raw(74, "5", false); }, 5, true, "5"},
        {70, [](GroupWriter& w) { w.write_table_count(70, 40001); }, 40001, false, ""},
        {70, [](GroupWriter& w) { w.write_table_count(70, 7); }, 7, false, ""},
    };
    for (const Case& c : cases) {
        const std::string what = "group " + std::to_string(c.code) + " = " + std::to_string(c.value);
        // ASCII
        {
            Written w = write_groups(DxfFormat::Ascii, BinaryCodeWidth::OneByte, c.write);
            const std::string value_line = c.text_kept ? c.text : printf_width(6, cut16(c.value));
            assert_eq(printf_width(3, c.code) + "\n" + value_line + "\n", w.bytes, what + ": ASCII");
        }
        // binary, one-byte and two-byte codes
        for (BinaryCodeWidth width : {BinaryCodeWidth::OneByte, BinaryCodeWidth::TwoBytes}) {
            Written w = write_groups(DxfFormat::Binary, width, c.write);
            std::vector<std::pair<int, std::string>> groups = read_binary_groups(w.bytes);
            assert_eq(1, static_cast<int>(groups.size()), what + ": binary has one group");
            assert_eq(c.code, groups[0].first, what + ": binary code");
            assert_eq(std::to_string(cut16(c.value)), groups[0].second, what + ": binary value");
        }
    }

    // A drawing: the polyface, in every version and format, written, read back and compared with AutoCAD's own file
    for (DxfVersion version : kVersions) {
        for (DxfFormat format : kFormats) {
            const std::string where = std::string(label(version)) + " " + label(format);
            hdxf::Drawing d(version);
            d.add_entity(make_polyface(kPolyfaceVertices));
            const std::vector<uint8_t> bytes = d.save_to_memory(format);
            assert_true(!bytes.empty(), where + ": " + d.last_error());
            if (format == DxfFormat::Ascii) {
                const std::string text(bytes.begin(), bytes.end());
                assert_true(text.find(" 71\r\n-25536\r\n") != std::string::npos, where + ": the POLYLINE's 71 is written cut");
                assert_true(text.find("\r\n 72\r\n-25537\r\n") != std::string::npos, where + ": a face index is written cut");
                assert_true(text.find("\r\n 73\r\n-25538\r\n") != std::string::npos, where + ": a face index is written cut");
            }
            hdxf::Drawing r;
            assert_true(r.load_from_memory(bytes.data(), bytes.size()), where + ": " + r.last_error());
            const hdxf::PolylineData* p = polyline_of(r);
            assert_true(p != nullptr, where + ": the polyface is read");
            assert_eq(-25536, p->m_count, where + ": 71 of the POLYLINE");
            assert_eq(kPolyfaceVertices + 1, static_cast<int>(p->vertices.size()), where + ": every vertex is written");
            const hdxf::Vertex& face = p->vertices.back();
            assert_eq(-25536, face.face[0], where + ": face index 71");
            assert_eq(-25537, face.face[1], where + ": face index 72");
            assert_eq(-25538, face.face[2], where + ": face index 73");
            assert_eq(0, face.face[3], where + ": face index 74");
            assert_eq(kPolyfaceVertices - 1, static_cast<int>(p->vertices[kPolyfaceVertices - 1].position.x), where + ": the vertices are in place");

            const std::filesystem::path facit = facit_file(version, "polyface", format);
            if (!facit.empty()) {
                hdxf::Drawing a;
                assert_true(a.load(facit), where + ": AutoCAD's file: " + a.last_error());
                const hdxf::PolylineData* ap = polyline_of(a);
                assert_true(ap != nullptr, where + ": AutoCAD's polyface");
                assert_eq(ap->m_count, p->m_count, where + ": 71 as AutoCAD writes it");
                assert_eq(static_cast<int>(ap->vertices.size()), static_cast<int>(p->vertices.size()), where + ": AutoCAD's vertices");
                const hdxf::Vertex& af = ap->vertices.back();
                for (int i = 0; i < 4; ++i) {
                    assert_eq(af.face[i], face.face[i], where + ": face index " + std::to_string(71 + i) + " as AutoCAD writes it");
                }
            }
        }
    }
}

TEST(wrapped_value_gives_warning) {
    // The writer counts per record type and code, from the last group 0
    for (DxfFormat format : kFormats) {
        const std::string where = label(format);
        Written w = write_groups(format, BinaryCodeWidth::TwoBytes, [](GroupWriter& writer) {
            writer.write_int16(90, 99999);                       // before any record of the stream's own
            writer.write_string(0, "POLYLINE");
            writer.write_int16(71, 40000);
            writer.write_int16(71, 70000);
            writer.write_int(71, 100000);
            writer.write_int16(72, 5);                           // in range: nothing
            writer.write_string(0, "VERTEX");
            writer.write_int16(71, 40000);
            writer.write_int16(72, 40001);
            writer.write_table_count(70, 40001);                 // a count: nothing
        });
        // binary has a SECTION record before the first group; ASCII has none
        const std::string first_record = format == DxfFormat::Binary ? "SECTION" : "DXF";
        const std::vector<std::string> expected = {
            first_record + " group 90: 1 value beyond 16 bits was written as AutoCAD writes it, cut to 16 bits (the first: 99999 as -31073)",
            "POLYLINE group 71: 3 values beyond 16 bits were written as AutoCAD writes them, cut to 16 bits (the first: 40000 as -25536)",
            "VERTEX group 71: 1 value beyond 16 bits was written as AutoCAD writes it, cut to 16 bits (the first: 40000 as -25536)",
            "VERTEX group 72: 1 value beyond 16 bits was written as AutoCAD writes it, cut to 16 bits (the first: 40001 as -25535)",
        };
        assert_eq(static_cast<int>(expected.size()), static_cast<int>(w.warnings.size()), where + ": one line per record type and code");
        for (size_t i = 0; i < expected.size() && i < w.warnings.size(); ++i) assert_eq(expected[i], w.warnings[i], where + ": line " + std::to_string(i));
    }
    // What is only captured is not counted; it is counted once it is written
    {
        std::ostringstream out(std::ios::binary);
        GroupWriter writer(out, DxfFormat::Binary, BinaryCodeWidth::TwoBytes);
        writer.write_sentinel();
        writer.write_string(0, "VERTEX");
        std::vector<hdxf::internal::CapturedGroup> sink;
        writer.capture(&sink);
        writer.write_int16(71, 40000);
        writer.capture(nullptr);
        assert_true(writer.wrapped_value_warnings().empty(), "captured groups are not counted");
        for (const auto& g : sink) writer.write_captured(g);
        assert_eq(1, static_cast<int>(writer.wrapped_value_warnings().size()), "and are counted when written");
    }
    // The text of a value held as text in ASCII stays; in binary it is cut and counted
    {
        Written ascii = write_groups(DxfFormat::Ascii, BinaryCodeWidth::OneByte, [](GroupWriter& w) {
            w.write_string(0, "VERTEX");
            w.write_typed(71, "40000");
            w.write_raw(72, "40000", false);
        });
        assert_true(ascii.warnings.empty(), "ASCII text is kept, no warning");
        Written binary = write_groups(DxfFormat::Binary, BinaryCodeWidth::TwoBytes, [](GroupWriter& w) {
            w.write_string(0, "VERTEX");
            w.write_typed(71, "40000");
            w.write_raw(72, "40000", false);
        });
        assert_eq(2, static_cast<int>(binary.warnings.size()), "binary cuts a value held as text and tells it");
    }
    // A drawing: warnings() says it after a save
    for (DxfVersion version : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2018}) {
        for (DxfFormat format : kFormats) {
            const std::string where = std::string(label(version)) + " " + label(format);
            hdxf::Drawing d(version);
            d.add_entity(make_polyface(kPolyfaceVertices));
            assert_true(!d.save_to_memory(format).empty(), where + ": " + d.last_error());
            assert_true(has_warning(d, "POLYLINE group 71: 1 value beyond 16 bits was written as AutoCAD writes it, cut to 16 bits "
                                       "(the first: 40000 as -25536)"),
                        where + ": a warning for the POLYLINE's 71");
            assert_true(has_warning(d, "VERTEX group 71: 1 value beyond 16 bits was written as AutoCAD writes it, cut to 16 bits "
                                       "(the first: 40000 as -25536)"),
                        where + ": a warning for the face index 71");
            assert_true(any_warning_with(d, "VERTEX group 72: 1 value"), where + ": a warning for the face index 72");
            assert_true(any_warning_with(d, "VERTEX group 73: 1 value"), where + ": a warning for the face index 73");
        }
    }
    // A drawing with every value in range tells nothing
    {
        hdxf::Drawing d(DxfVersion::R2000);
        d.add_entity(make_polyface(100));
        assert_true(!d.save_to_memory(DxfFormat::Ascii).empty(), d.last_error());
        assert_false(any_warning_with(d, "beyond 16 bits"), "nothing is beyond 16 bits");
    }
}

TEST(read_ascii_text_kept) {
    // The writer: the text of a value in an ASCII file is written as it was
    {
        Written w = write_groups(DxfFormat::Ascii, BinaryCodeWidth::OneByte, [](GroupWriter& writer) {
            writer.write_raw(71, "40000", false);
            writer.write_raw(71, "     40000", false);
            writer.write_raw(71, " 40000", false);
            writer.write_raw(72, "-40000", false);
            writer.write_typed(73, "40000");
            writer.write_typed(73, "5");
        });
        assert_eq(std::string(" 71\n40000\n 71\n     40000\n 71\n 40000\n 72\n-40000\n 73\n40000\n 73\n     5\n"), w.bytes,
                  "a number beyond 16 bits stays the text it was, one in range is aligned");
        assert_true(w.warnings.empty(), "no warning for text that is kept");
    }
    // A file: a value beyond 16 bits (not what AutoCAD writes) is read, and written back as the file had it
    for (DxfVersion version : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2018}) {
        for (const char* replacement : {" 40000", "40000", "    40000"}) {
            const std::string where = std::string(label(version)) + " \"" + replacement + "\"";
            hdxf::Drawing d(version);
            d.add_entity(make_polyface(3));
            hdxf::Entity* polyline = nullptr;
            for (hdxf::Entity& e : d.entities()) {
                if (e.type == hdxf::EntityType::Polyline) polyline = &e;
            }
            assert_true(polyline != nullptr, where + ": the polyface");
            polyline->as_polyline().m_count = 40000;
            const std::vector<uint8_t> written = d.save_to_memory(DxfFormat::Ascii);
            assert_true(!written.empty(), where + ": " + d.last_error());
            std::string text(written.begin(), written.end());
            const size_t at = text.find("\r\n-25536\r\n");
            assert_true(at != std::string::npos, where + ": the value is written cut");
            assert_true(text.find("\r\n-25536\r\n", at + 1) == std::string::npos, where + ": once");
            text.replace(at + 2, 6, replacement);
            const std::vector<uint8_t> file = bytes_of(text);
            hdxf::Drawing r;
            assert_true(r.load_from_memory(file.data(), file.size()), where + ": " + r.last_error());
            const hdxf::PolylineData* p = polyline_of(r);
            assert_true(p != nullptr, where + ": read");
            assert_eq(40000, p->m_count, where + ": the number is read as it is");
            const std::vector<uint8_t> back = r.save_to_memory(DxfFormat::Ascii);
            assert_true(back == file, where + ": written back as it was read");
            assert_false(any_warning_with(r, "beyond 16 bits"), where + ": and it is not told");
        }
    }
}

TEST(signed_flags_keep_bits) {
    // A bit pattern held as a negative number is written as it is: nothing is cut, nothing is told
    const int values[] = {-1, -32768, -25536, -28672, 32767, 0, 1, -2, 0x7FFF, 0x8000 - 65536};
    for (DxfFormat format : kFormats) {
        for (BinaryCodeWidth width : {BinaryCodeWidth::OneByte, BinaryCodeWidth::TwoBytes}) {
            Written w = write_groups(format, width, [&](GroupWriter& writer) {
                for (int v : values) {
                    writer.write_int16(70, v);
                    writer.write_int(70, v);
                    writer.write_typed(70, std::to_string(v));
                    writer.write_raw(70, std::to_string(v), format == DxfFormat::Binary);
                }
            });
            const std::string where = std::string(label(format)) + (width == BinaryCodeWidth::OneByte ? " one-byte codes" : " two-byte codes");
            assert_true(w.warnings.empty(), where + ": nothing is told");
            if (format == DxfFormat::Binary) {
                std::vector<std::pair<int, std::string>> groups = read_binary_groups(w.bytes);
                assert_eq(static_cast<int>(sizeof(values) / sizeof(values[0])) * 4, static_cast<int>(groups.size()), where + ": every group");
                size_t at = 0;
                for (int v : values) {
                    for (int k = 0; k < 4; ++k, ++at) {
                        assert_eq(70, groups[at].first, where + ": code");
                        assert_eq(std::to_string(v), groups[at].second, where + ": the value " + std::to_string(v) + " keeps its bits");
                    }
                }
            } else {
                std::string expected;
                for (int v : values) {
                    for (int k = 0; k < 3; ++k) expected += " 70\n" + printf_width(6, v) + "\n";
                    expected += " 70\n" + std::to_string(v) + "\n";          // the raw text as it was
                }
                assert_eq(expected, w.bytes, where + ": ASCII");
            }
        }
    }
    // A value read from a binary file is a signed number, and written back it is the same
    {
        Written w = write_groups(DxfFormat::Binary, BinaryCodeWidth::TwoBytes, [](GroupWriter& writer) { writer.write_int16(70, -1); });
        std::vector<std::pair<int, std::string>> groups = read_binary_groups(w.bytes);
        assert_eq(std::string("-1"), groups[0].second, "0xFFFF is read as -1");
    }
}

TEST(table_with_40000_layers_complete) {
    constexpr int kLayers = 40000;
    for (DxfVersion version : kVersions) {
        for (DxfFormat format : kFormats) {
            const std::string where = std::string(label(version)) + " " + label(format);
            hdxf::Drawing d = make_layers(version, kLayers);
            const int made = static_cast<int>(d.layers().size());
            assert_true(made > 32767, where + ": more layers than 16 bits count");
            const std::vector<uint8_t> bytes = d.save_to_memory(format);
            assert_true(!bytes.empty(), where + ": " + d.last_error());
            int count = 0;
            assert_true(table_count(bytes, "LAYER", count), where + ": the LAYER table has a count");
            assert_eq(cut16(made), count, where + ": the count is cut to 16 bits, as AutoCAD writes it");
            hdxf::Drawing r;
            assert_true(r.load_from_memory(bytes.data(), bytes.size()), where + ": " + r.last_error());
            assert_eq(made, static_cast<int>(r.layers().size()), where + ": every layer is written and read");
            for (size_t i = 0; i < d.layers().size(); ++i) {
                if (d.layers()[i].name != r.layers()[i].name) {
                    assert_true(false, where + ": layer " + std::to_string(i) + " is " + r.layers()[i].name + ", not " + d.layers()[i].name);
                }
            }
            bool line_on_last = false;
            for (const hdxf::Entity& e : r.entities()) {
                if (e.type == hdxf::EntityType::Line && e.props.layer == "L" + std::to_string(kLayers - 1)) line_on_last = true;
            }
            assert_true(line_on_last, where + ": the line on the last layer");

            const std::filesystem::path facit = facit_file(version, "layers", format);
            if (!facit.empty()) {
                hdxf::Drawing a;
                assert_true(a.load(facit), where + ": AutoCAD's file: " + a.last_error());
                std::ifstream in(facit, std::ios::binary);
                std::vector<uint8_t> acad((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                int acad_count = 0;
                assert_true(table_count(acad, "LAYER", acad_count), where + ": AutoCAD's LAYER table has a count");
                assert_eq(cut16(static_cast<long long>(a.layers().size())), acad_count, where + ": AutoCAD cuts the count the same way");
                assert_true(a.layers().size() > 32767, where + ": AutoCAD's file has all its layers");
            }
        }
    }
}

TEST(table_count_wraps_silently) {
    // The writer: the count is cut in both formats, and nothing is counted as a wrapped value
    for (DxfFormat format : kFormats) {
        Written w = write_groups(format, BinaryCodeWidth::TwoBytes, [](GroupWriter& writer) {
            writer.write_string(0, "TABLE");
            writer.write_table_count(70, 40001);
            writer.write_table_count(70, 65536);
            writer.write_table_count(70, 12);
        });
        assert_true(w.warnings.empty(), std::string(label(format)) + ": no warning for a count");
        if (format == DxfFormat::Binary) {
            std::vector<std::pair<int, std::string>> groups = read_binary_groups(w.bytes);
            assert_eq(4, static_cast<int>(groups.size()));
            assert_eq(std::string("-25535"), groups[1].second, "40001 is cut");
            assert_eq(std::string("0"), groups[2].second, "65536 is cut");
            assert_eq(std::string("12"), groups[3].second, "12 stays");
        } else {
            assert_eq(std::string("  0\nTABLE\n 70\n-25535\n 70\n     0\n 70\n    12\n"), w.bytes, "ASCII");
        }
    }
    // A drawing: 40 000 layers give the count without a warning, and the other 16-bit values of the file are still told
    for (DxfVersion version : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2018}) {
        for (DxfFormat format : kFormats) {
            const std::string where = std::string(label(version)) + " " + label(format);
            hdxf::Drawing d = make_layers(version, 40000);
            const std::vector<uint8_t> bytes = d.save_to_memory(format);
            assert_true(!bytes.empty(), where + ": " + d.last_error());
            assert_false(any_warning_with(d, "beyond 16 bits"), where + ": no warning for the table's count");
            assert_false(any_warning_with(d, "TABLE group 70"), where + ": none for TABLE 70");
        }
    }
    // A table read from a file that grows beyond the range: its head goes through the merge of a read record
    for (DxfVersion version : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2018}) {
        for (DxfFormat format : kFormats) {
            const std::string where = std::string(label(version)) + " " + label(format) + " read";
            hdxf::Drawing made = make_layers(version, 3);
            const std::vector<uint8_t> small = made.save_to_memory(format);
            hdxf::Drawing read;
            assert_true(read.load_from_memory(small.data(), small.size()), where + ": " + read.last_error());
            for (int k = 0; k < 40000; ++k) {
                hdxf::Layer layer;
                layer.name = "N" + std::to_string(k);
                read.layers().push_back(std::move(layer));
            }
            const std::vector<uint8_t> bytes = read.save_to_memory(format);
            assert_true(!bytes.empty(), where + ": " + read.last_error());
            assert_false(any_warning_with(read, "beyond 16 bits"), where + ": no warning for the table's count");
        }
    }
}
