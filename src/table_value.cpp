#include "TableTypes.hpp"
#include <cstring>

// AcValue (phase 9, step 9.1): the typed value of a table cell, of a cell's custom data and of a content
// (AutoCAD's own files: doc/facit/acad_*_dxf_tables.dxf, ACadSharp's sample_AC1015 … AC1032), the markers
// the table records are written with, and the text checksum of ACAD_ROUNDTRIP_2008_CELL_CHECKSUM.

namespace hdxf {

// ---------------------------------------------------------------------------
// The public constructors
// ---------------------------------------------------------------------------

TableValue TableValue::from_text(const std::string& text) {
    TableValue v;
    v.type = TableValueType::String;
    v.flags = 6;
    v.value = text;
    v.text = text;
    return v;
}

TableValue TableValue::from_double(double d, const std::string& format, const std::string& text) {
    TableValue v;
    v.type = TableValueType::Double;
    v.flags = format.empty() ? 6 : 4;
    v.value = d;
    v.format = format;
    v.text = text;
    return v;
}

TableValue TableValue::from_long(int32_t l, const std::string& format, const std::string& text) {
    TableValue v;
    v.type = TableValueType::Long;
    v.flags = format.empty() ? 6 : 4;
    v.value = l;
    v.format = format;
    v.text = text;
    return v;
}

bool TableValue::operator==(const TableValue& o) const {
    if (type != o.type || flags != o.flags || unit_type != o.unit_type || format != o.format || text != o.text ||
        value.index() != o.value.index()) {
        return false;
    }
    if (const Point2* a = std::get_if<Point2>(&value)) {
        const Point2& b = std::get<Point2>(o.value);
        return a->x == b.x && a->y == b.y;
    }
    if (const Point3* a = std::get_if<Point3>(&value)) {
        const Point3& b = std::get<Point3>(o.value);
        return a->x == b.x && a->y == b.y && a->z == b.z;
    }
    return std::visit(
        [&](const auto& a) -> bool {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, Point2> || std::is_same_v<T, Point3> || std::is_same_v<T, std::monostate>) {
                return true;
            } else {
                return a == std::get<T>(o.value);
            }
        },
        value);
}

namespace internal {

// ---------------------------------------------------------------------------
// Markers
// ---------------------------------------------------------------------------

namespace {

std::string trimmed_text(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return s.substr(b, e - b);
}

} // namespace

bool at_marker(const GroupCursor& c, int code, const char* text) {
    const DxfGroup* g = c.peek();
    return g && g->code == code && trimmed_text(g->value) == text;
}

bool take_marker(GroupCursor& c, int code, const char* text) {
    if (!at_marker(c, code, text)) return false;
    std::string skipped;
    c.string(code, skipped);
    return true;
}

bool require_marker(GroupCursor& c, int code, const char* text) {
    if (take_marker(c, code, text)) return true;
    const DxfGroup* g = c.peek();
    c.fail(std::string("the marker ") + std::to_string(code) + " " + text + " is missing" +
           (g ? " (group " + std::to_string(g->code) + " \"" + g->value + "\" is there)" : " (the subclass ends)"));
    return false;
}

bool take_u32(GroupCursor& c, int code, uint32_t& out) {
    int64_t v = 0;
    if (!c.integer(code, v)) return false;
    if (v < -2147483648LL || v > 4294967295LL) {
        c.fail("group " + std::to_string(code) + " is " + std::to_string(v) + ", which does not fit 32 bits");
        return false;
    }
    out = static_cast<uint32_t>(v);
    return true;
}

bool require_u32(GroupCursor& c, int code, uint32_t& out) {
    if (take_u32(c, code, out)) return true;
    if (c.ok()) c.fail("group " + std::to_string(code) + " is missing");
    return false;
}

// UTF-16 units of a UTF-8 text (an invalid byte counts as one unit)
static std::vector<uint16_t> utf16_of(const std::string& s) {
    std::vector<uint16_t> out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        uint32_t cp = b;
        size_t len = 1;
        if (b >= 0xF0 && i + 3 < s.size() + 0 && (b & 0xF8) == 0xF0) { cp = b & 0x07; len = 4; }
        else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; len = 3; }
        else if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; len = 2; }
        if (len > 1) {
            if (i + len > s.size()) { cp = b; len = 1; }
            else {
                bool ok = true;
                for (size_t k = 1; k < len; ++k) {
                    unsigned char cb = static_cast<unsigned char>(s[i + k]);
                    if ((cb & 0xC0) != 0x80) { ok = false; break; }
                    cp = (cp << 6) | (cb & 0x3F);
                }
                if (!ok) { cp = b; len = 1; }
            }
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<uint16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<uint16_t>(cp));
        }
        i += len;
    }
    return out;
}

static std::string utf8_of(const std::vector<uint16_t>& units, size_t from, size_t to) {
    std::string out;
    for (size_t i = from; i < to; ++i) {
        uint32_t cp = units[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < to && units[i + 1] >= 0xDC00 && units[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (units[i + 1] - 0xDC00);
            ++i;
        }
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

bool read_chunked_text(GroupCursor& c, int chunk_code, int last_code, std::string& out) {
    std::string text;
    bool any = false;
    std::string piece;
    while (c.code() == chunk_code) {
        c.string(chunk_code, piece);
        text += piece;
        any = true;
    }
    if (c.string(last_code, piece)) {
        text += piece;
        any = true;
    } else if (any) {
        c.fail("a text in pieces (group " + std::to_string(chunk_code) + ") does not end in group " +
               std::to_string(last_code));
        return false;
    }
    if (any) out = text;
    return any;
}

void write_chunked_text(RecordWriter& w, int chunk_code, int last_code, const std::string& text) {
    constexpr size_t kPiece = 250;
    std::vector<uint16_t> units = utf16_of(text);
    if (units.size() < kPiece) {
        w.text(last_code, text);
        return;
    }
    size_t at = 0;
    while (units.size() - at >= kPiece) {
        size_t end = at + kPiece;
        // not between the halves of a surrogate pair
        if (units[end - 1] >= 0xD800 && units[end - 1] < 0xDC00) --end;
        w.text(chunk_code, utf8_of(units, at, end));
        at = end;
    }
    w.text(last_code, utf8_of(units, at, units.size()));
}

// ---------------------------------------------------------------------------
// Dates and the checksum
// ---------------------------------------------------------------------------

bool table_date_from_bytes(const std::vector<uint8_t>& b, TableDate& out) {
    if (b.size() != 16) return false;
    auto at = [&](size_t i) { return static_cast<uint16_t>(b[i * 2] | (b[i * 2 + 1] << 8)); };
    out.year = at(0);
    out.month = at(1);
    out.day_of_week = at(2);
    out.day = at(3);
    out.hour = at(4);
    out.minute = at(5);
    out.second = at(6);
    out.millisecond = at(7);
    return true;
}

std::vector<uint8_t> table_date_to_bytes(const TableDate& d) {
    std::vector<uint8_t> out;
    for (uint16_t v : {d.year, d.month, d.day_of_week, d.day, d.hour, d.minute, d.second, d.millisecond}) {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>(v >> 8));
    }
    return out;
}

double table_text_checksum(const std::string& text) {
    uint64_t sum = 0;
    uint64_t position = 1;
    for (uint16_t unit : utf16_of(text)) sum += position++ * unit;
    return static_cast<double>(sum);
}

std::string table_value_text(const TableValue& v) {
    if (!v.text.empty()) return v.text;
    if (const std::string* s = std::get_if<std::string>(&v.value)) return *s;
    return {};
}

// ---------------------------------------------------------------------------
// AcValue
// ---------------------------------------------------------------------------

namespace {

bool known_type(int32_t t) {
    switch (t) {
        case 0: case 1: case 2: case 4: case 8: case 16: case 32: case 64: case 128: case 256: case 512:
            return true;
        default:
            return false;
    }
}

} // namespace

bool read_ac_value(GroupCursor& c, bool full_form, TableValue& out, bool followed_by_91) {
    out = TableValue();
    if (full_form) {
        if (!require_u32(c, 93, out.flags)) return false;
    }
    int32_t type = 0;
    if (!c.require_integer(90, type)) return false;
    if (!known_type(type)) {
        c.fail("the value type " + std::to_string(type) + " of a table value is not established");
        return false;
    }
    out.type = static_cast<TableValueType>(type);
    switch (out.type) {
        case TableValueType::Long: {
            int32_t v = 0;
            if (c.integer(91, v)) out.value = v;
            break;
        }
        case TableValueType::Double: {
            double v = 0.0;
            if (c.real(140, v)) out.value = v;
            break;
        }
        case TableValueType::String: {
            std::string v;
            if (c.string(1, v)) out.value = v;
            break;
        }
        case TableValueType::Date:
        case TableValueType::Buffer: {
            int32_t size = 0;
            if (c.integer(92, size)) {
                if (size < 0) {
                    c.fail("a table value's byte count is negative");
                    return false;
                }
                std::vector<uint8_t> bytes;
                c.bytes(310, bytes);
                if (!c.ok()) return false;
                if (bytes.size() != static_cast<size_t>(size)) {
                    c.fail("a table value says " + std::to_string(size) + " bytes and has " + std::to_string(bytes.size()));
                    return false;
                }
                if (out.type == TableValueType::Date) {
                    // 2007 and later: a SYSTEMTIME of 16 bytes; 2000 and 2004: eight bytes (a 64-bit count of
                    // seconds since 1970, or -1), kept as the bytes
                    TableDate date;
                    if (bytes.size() == 16 && table_date_from_bytes(bytes, date)) {
                        out.value = date;
                    } else if (bytes.size() == 8) {
                        out.value = std::move(bytes);
                    } else {
                        c.fail("a table date has " + std::to_string(bytes.size()) + " bytes, not 16 or 8");
                        return false;
                    }
                } else {
                    out.value = std::move(bytes);
                }
            }
            break;
        }
        case TableValueType::Point2D: {
            Point2 p;
            if (c.point2(11, p)) out.value = p;
            break;
        }
        case TableValueType::Point3D: {
            Point3 p;
            if (c.point3(11, p)) out.value = p;
            break;
        }
        case TableValueType::ObjectId: {
            std::string h;
            if (c.handle(330, h)) out.value = TableHandle{h};
            break;
        }
        case TableValueType::Unset: {
            // 2000 and 2004 write an empty value with a long 0 (91) before the cell content's own 91: two 91 in a row
            const DxfGroup* first = c.peek(0);
            const DxfGroup* second = c.peek(1);
            // The full form too, where a program other than AutoCAD writes it: TurboCAD's FIELD records have `93 3, 90 0, 91 0`
            // as their cache (tc_echocal_blockdiag.dxf); the 94 that follows in this form makes it unambiguous. AutoCAD 2025
            // reads it and writes the value without the 91 (acad_2007_dxf_ext_tc_echocal_blockdiag.dxf)
            const bool long_zero = first && first->code == 91 && (full_form || !followed_by_91 || (second && second->code == 91));
            if (long_zero) {
                int32_t v = 0;
                if (c.integer(91, v)) out.value = v;
            }
            break;
        }
        case TableValueType::ResBuf:
        case TableValueType::General:
            break;
    }
    if (!c.ok()) return false;
    if (full_form) {
        if (!c.require_integer(94, out.unit_type)) return false;
        if (!c.require_string(300, out.format)) return false;
        if (!read_chunked_text(c, 303, 302, out.text)) {
            if (c.ok()) c.fail("a table value's text (302) is missing");
            return false;
        }
        if (!require_marker(c, 304, "ACVALUE_END")) return false;
    }
    return c.ok();
}

void write_ac_value(RecordWriter& w, bool full_form, const TableValue& v) {
    if (full_form) w.integer(93, static_cast<int32_t>(v.flags));
    w.integer(90, static_cast<int32_t>(v.type));
    auto mismatch = [&]() {
        w.fail("A table value of type " + std::to_string(static_cast<int>(v.type)) + " holds a value of another type");
    };
    // 2000 and 2004 write an empty value with a long 0
    if (!full_form && !v.has_value()) {
        // The old form has no value-less type: a Long, Double or String without its value is the zero or empty one
        // (read back, its trailing 91 would be taken for the long otherwise)
        switch (v.type) {
            case TableValueType::Unset: w.integer(91, 0); break;
            case TableValueType::Long: w.integer(91, 0); break;
            case TableValueType::Double: w.real(140, 0.0); break;
            case TableValueType::String: w.text(1, ""); break;
            default: break;
        }
    }
    if (v.has_value()) {
        switch (v.type) {
            case TableValueType::Long:
                if (const int32_t* x = std::get_if<int32_t>(&v.value)) w.integer(91, *x); else mismatch();
                break;
            case TableValueType::Double:
                if (const double* x = std::get_if<double>(&v.value)) w.real(140, *x); else mismatch();
                break;
            case TableValueType::String:
                if (const std::string* x = std::get_if<std::string>(&v.value)) w.text(1, *x); else mismatch();
                break;
            case TableValueType::Date:
                if (const TableDate* x = std::get_if<TableDate>(&v.value)) {
                    std::vector<uint8_t> bytes = table_date_to_bytes(*x);
                    w.integer(92, static_cast<int64_t>(bytes.size()));
                    w.bytes(310, bytes);
                } else if (const auto* buffer = std::get_if<std::vector<uint8_t>>(&v.value)) {
                    w.integer(92, static_cast<int64_t>(buffer->size()));
                    w.bytes(310, *buffer);
                } else {
                    mismatch();
                }
                break;
            case TableValueType::Buffer:
                if (const auto* x = std::get_if<std::vector<uint8_t>>(&v.value)) {
                    w.integer(92, static_cast<int64_t>(x->size()));
                    w.bytes(310, *x);
                } else {
                    mismatch();
                }
                break;
            case TableValueType::Point2D:
                if (const Point2* x = std::get_if<Point2>(&v.value)) w.point2(11, *x); else mismatch();
                break;
            case TableValueType::Point3D:
                if (const Point3* x = std::get_if<Point3>(&v.value)) w.point3(11, *x); else mismatch();
                break;
            case TableValueType::ObjectId:
                if (const TableHandle* x = std::get_if<TableHandle>(&v.value)) {
                    w.pointer(330, x->handle.empty() ? std::string("0") : x->handle, PointerWrite::ZeroDead);
                } else {
                    mismatch();
                }
                break;
            case TableValueType::Unset:
                // the long 0 2000 and 2004 write for an empty value (see read_ac_value)
                if (const int32_t* x = std::get_if<int32_t>(&v.value)) w.integer(91, *x); else mismatch();
                break;
            case TableValueType::ResBuf:
            case TableValueType::General:
                mismatch();         // these have no value in any AutoCAD file
                break;
        }
    }
    if (full_form) {
        w.integer(94, v.unit_type);
        w.text(300, v.format);
        write_chunked_text(w, 303, 302, v.text);
        w.text(304, "ACVALUE_END");
    }
}

} // namespace internal
} // namespace hdxf
