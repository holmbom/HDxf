#include "group_io.hpp"
#include "text_encoding.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hdxf {
namespace internal {

static const char binary_sentinel[] = "AutoCAD Binary DXF\r\n\x1a";   // + '\0' = 22 bytes

// ---------------------------------------------------------------------------
// Binary helpers
// ---------------------------------------------------------------------------

static int16_t read_int16_le(std::istream& in) {
    char buf[2];
    in.read(buf, 2);
    uint16_t u = static_cast<uint8_t>(buf[0]) |
                 (static_cast<uint8_t>(buf[1]) << 8);
    return static_cast<int16_t>(u);
}

static int32_t read_int32_le(std::istream& in) {
    char buf[4];
    in.read(buf, 4);
    uint32_t u = static_cast<uint8_t>(buf[0]) |
                 (static_cast<uint8_t>(buf[1]) << 8) |
                 (static_cast<uint8_t>(buf[2]) << 16) |
                 (static_cast<uint32_t>(static_cast<uint8_t>(buf[3])) << 24);
    return static_cast<int32_t>(u);
}

static double read_double_le(std::istream& in) {
    char buf[8];
    in.read(buf, 8);
    double v;
    std::memcpy(&v, buf, 8);
    return v;
}

static std::string read_null_terminated(std::istream& in) {
    std::string result;
    char c;
    while (in.get(c) && c != '\0') {
        result += c;
    }
    return result;
}

static void write_int16_le(std::ostream& out, int16_t v) {
    uint16_t u = static_cast<uint16_t>(v);
    char buf[2];
    buf[0] = static_cast<char>(u & 0xFF);
    buf[1] = static_cast<char>((u >> 8) & 0xFF);
    out.write(buf, 2);
}

static void write_int32_le(std::ostream& out, int32_t v) {
    uint32_t u = static_cast<uint32_t>(v);
    char buf[4];
    buf[0] = static_cast<char>(u & 0xFF);
    buf[1] = static_cast<char>((u >> 8) & 0xFF);
    buf[2] = static_cast<char>((u >> 16) & 0xFF);
    buf[3] = static_cast<char>((u >> 24) & 0xFF);
    out.write(buf, 4);
}

static void write_double_le(std::ostream& out, double v) {
    char buf[8];
    std::memcpy(buf, &v, 8);
    out.write(buf, 8);
}

// ---------------------------------------------------------------------------
// GroupReader
// ---------------------------------------------------------------------------

GroupReader::GroupReader(std::istream& in)
    : in_(in)
{
}

void GroupReader::detect_format() {
    char buf[22];
    in_.read(buf, 22);

    if (!in_.good()) {
        in_.clear();
        in_.seekg(0);
        binary_ = false;
        return;
    }

    if (std::memcmp(buf, binary_sentinel, 21) == 0 && buf[21] == '\0') {
        binary_ = true;
        char next[2] = {1, 1};
        in_.read(next, 2);
        in_.clear();
        in_.seekg(22);
        width_ = (next[0] == '\0' && next[1] == '\0') ? BinaryCodeWidth::TwoBytes
                                                      : BinaryCodeWidth::OneByte;
        return;
    }

    // Not binary — seek back to start
    in_.seekg(0);
    binary_ = false;
}

bool GroupReader::read(GroupCode& gc) {
    bool ok = binary_ ? read_binary(gc) : read_ascii(gc);
    if (ok) ++count_;
    return ok;
}

bool GroupReader::read_ascii(GroupCode& gc) {
    std::string code_line;
    if (!std::getline(in_, code_line)) return false;

    std::string value_line;
    if (!std::getline(in_, value_line)) return false;

    // Trim whitespace from code line
    while (!code_line.empty() && (code_line.back() == '\r' || code_line.back() == ' '))
        code_line.pop_back();
    std::string untrimmed_front = code_line;
    while (!code_line.empty() && code_line.front() == ' ')
        code_line.erase(code_line.begin());

    // Only the line ending is removed from the value — trailing spaces are part of
    // string values such as TEXT content
    if (!value_line.empty() && value_line.back() == '\r')
        value_line.pop_back();

    // The code line must be an integer — atoi would read anything as group 0
    char* end = nullptr;
    long code = std::strtol(code_line.c_str(), &end, 10);
    if (code_line.empty() || *end != '\0') {
        error_ = "Invalid group code \"" + code_line + "\".";
        return false;
    }
    gc.code = static_cast<int>(code);
    gc.value = value_line;
    // The width of the code line where it is not three columns, when it is the code right-aligned in it
    gc.code_columns = 0;
    if (untrimmed_front.size() <= 7) {
        char aligned[16];
        std::snprintf(aligned, sizeof(aligned), "%*ld", static_cast<int>(untrimmed_front.size()), code);
        if (untrimmed_front == aligned && untrimmed_front.size() != 3) {
            gc.code_columns = static_cast<int>(untrimmed_front.size());
        }
    }

    return true;
}

bool GroupReader::read_binary(GroupCode& gc) {
    if (width_ == BinaryCodeWidth::TwoBytes) {
        gc.code = static_cast<uint16_t>(read_int16_le(in_));
        if (!in_.good()) return false;
    } else {
        char first_byte;
        if (!in_.get(first_byte)) return false;
        uint8_t fb = static_cast<uint8_t>(first_byte);
        if (fb == 255) {
            gc.code = static_cast<uint16_t>(read_int16_le(in_));
            if (!in_.good()) return false;
        } else {
            gc.code = fb;
        }
    }

    switch (group_value_type(gc.code, record_)) {
        case GroupValueType::String:
            gc.value = read_null_terminated(in_);
            if (gc.code == 0) record_ = gc.value;
            break;
        case GroupValueType::Double: {
            double d = read_double_le(in_);
            if (!in_.good()) return false;
            char buf[64];
            if (std::isnan(d)) {
                // A NaN keeps its bits (AutoCAD writes uninitialised memory in a few R12 records)
                uint64_t bits;
                std::memcpy(&bits, &d, 8);
                std::snprintf(buf, sizeof(buf), "nan(0x%016llX)", static_cast<unsigned long long>(bits));
            } else {
                std::snprintf(buf, sizeof(buf), "%.17g", d);
            }
            gc.value = buf;
            break;
        }
        case GroupValueType::Int16: {
            int16_t v = read_int16_le(in_);
            if (!in_.good()) return false;
            gc.value = std::to_string(v);
            break;
        }
        case GroupValueType::Int32: {
            int32_t v = read_int32_le(in_);
            if (!in_.good()) return false;
            gc.value = std::to_string(v);
            break;
        }
        case GroupValueType::Int64: {
            char buf[8];
            in_.read(buf, 8);
            if (!in_.good()) return false;
            int64_t v;
            std::memcpy(&v, buf, 8);
            gc.value = std::to_string(v);
            break;
        }
        case GroupValueType::Bool: {
            char b;
            in_.get(b);
            if (!in_.good()) return false;
            // The byte as it is: a group that is a flag by its code can hold a number (SUNSTUDY's hours, group 290)
            gc.value = std::to_string(static_cast<unsigned>(static_cast<unsigned char>(b)));
            break;
        }
        case GroupValueType::BinaryChunk: {
            // Length byte + raw data, held as hex text like in ASCII DXF
            char len_byte;
            if (!in_.get(len_byte)) return false;
            uint8_t len = static_cast<uint8_t>(len_byte);
            std::string raw(len, '\0');
            if (len > 0) in_.read(&raw[0], len);
            gc.value.clear();
            gc.value.reserve(len * 2);
            for (uint8_t j = 0; j < len; ++j) {
                char hex[3];
                std::snprintf(hex, sizeof(hex), "%02X", static_cast<uint8_t>(raw[j]));
                gc.value += hex;
            }
            break;
        }
        case GroupValueType::Unknown:
            // The size of the value is unknown, so nothing after it can be read
            error_ = "Unknown group code " + std::to_string(gc.code) + " in binary DXF.";
            return false;
    }

    return in_.good() || in_.eof();
}

// ---------------------------------------------------------------------------
// GroupWriter
// ---------------------------------------------------------------------------

GroupWriter::GroupWriter(std::ostream& out, DxfFormat format, BinaryCodeWidth width)
    : out_(&out), format_(format), width_(width)
{
}

void GroupWriter::write_sentinel() {
    out_->write(binary_sentinel, 21);
    out_->put('\0');
}

void GroupWriter::write_code_binary(int code) {
    if (width_ == BinaryCodeWidth::TwoBytes) {
        write_int16_le(*out_, static_cast<int16_t>(code));
    } else if (code >= 0 && code <= 254) {
        out_->put(static_cast<char>(code));
    } else {
        // Escape byte 255 + 2-byte actual code for codes >= 255
        out_->put(static_cast<char>(255));
        write_int16_le(*out_, static_cast<int16_t>(code));
    }
}

void GroupWriter::write_string(int code, const std::string& value) {
    if (sink_) {
        CapturedGroup g;
        g.code = code;
        g.text = value;
        sink_->push_back(std::move(g));
        return;
    }
    note_record(code, value);
    std::string text = value;
    if (needs_text_conversion(value)) {
        if (!encode_text(value, code_page_, text)) {
            if (error_.empty()) {
                error_ = "Invalid UTF-8 in group code " + std::to_string(code) +
                         ": strings must be UTF-8";
            }
            text = value;
        }
        if (format_ != DxfFormat::Binary) text = encode_carets(text, code_page_);
    }

    if (format_ == DxfFormat::Binary) {
        write_code_binary(code);
        out_->write(text.c_str(), text.size() + 1); // null-terminated
    } else {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%*d", plain_ ? 1 : 3, code);
        *out_ << buf << "\n" << text << "\n";
    }
}

static CapturedGroup captured_int(int code, int64_t value) {
    CapturedGroup g;
    g.code = code;
    g.kind = CapturedGroup::Kind::Int;
    g.i = value;
    return g;
}

// ASCII integers are right-aligned as AutoCAD writes them (R12 and 2000-2018 alike): 16-
// and 8-bit values and booleans in 6 characters, 32-bit in 9, 64-bit in 18
std::string GroupWriter::aligned(long long value, int width) const {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%*lld", plain_ ? 1 : width, value);
    return buf;
}

void GroupWriter::write_int16(int code, int value) {
    if (sink_) {
        sink_->push_back(captured_int(code, value));
        return;
    }
    if (format_ == DxfFormat::Binary) {
        write_code_binary(code);
        write_int16_le(*out_, static_cast<int16_t>(value));
    } else {
        write_string(code, aligned(value, 6));
    }
}

void GroupWriter::write_int32(int code, int32_t value) {
    if (sink_) {
        sink_->push_back(captured_int(code, value));
        return;
    }
    if (format_ == DxfFormat::Binary) {
        write_code_binary(code);
        write_int32_le(*out_, value);
    } else {
        write_string(code, aligned(value, 9));
    }
}

void GroupWriter::write_double(int code, double value) {
    if (sink_) {
        CapturedGroup g;
        g.code = code;
        g.kind = CapturedGroup::Kind::Double;
        g.d = value;
        sink_->push_back(g);
        return;
    }
    if (format_ == DxfFormat::Binary) {
        write_code_binary(code);
        write_double_le(*out_, value);
        return;
    }

    write_string(code, format_double(value, number_style_));
}

// Trailing zeros after the decimal point go, but one decimal digit stays ("1.0")
static void trim_zeros(char* buf) {
    char* dot = std::strchr(buf, '.');
    if (!dot) return;
    char* end = buf + std::strlen(buf) - 1;
    while (end > dot + 1 && *end == '0') {
        *end = '\0';
        end--;
    }
}

std::string format_double(double value, NumberStyle style) {
    char buf[400];
    double abs_val = std::fabs(value);

    if (style == NumberStyle::R12) {
        // As AutoCAD writes R12: 16 decimal places (DXFOUT's default precision), trailing
        // zeros dropped — also for tiny values (-0.0000000000000001). Only the 1e20
        // sentinels and other values from 1e16 on take an exponent, with 16 decimals in R12
        // ("1.0000000000000000E+20", AutoCAD 2025's R12 files; 2000+ has 15).
        if (!std::isfinite(value) || abs_val >= 1e16) {
            std::snprintf(buf, sizeof(buf), "%.16E", value);
        } else {
            std::snprintf(buf, sizeof(buf), "%.16f", value);
            trim_zeros(buf);
        }
        return buf;
    }

    if (value == 0.0) return "0.0";
    bool tiny_exponent = style == NumberStyle::AutoCadExponent && abs_val < 1e-15;
    if (!std::isfinite(value) || abs_val >= 1e16 || tiny_exponent) {
        std::snprintf(buf, sizeof(buf), "%.15E", value);
        return buf;
    }
    // 16 significant digits, but never more than 16 decimals (AutoCAD writes
    // -0.0000000114653187, not ever smaller digits)
    int exponent = static_cast<int>(std::floor(std::log10(abs_val)));
    int decimals = std::clamp(15 - exponent, 1, 16);
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    trim_zeros(buf);
    return buf;
}

void GroupWriter::write_int(int code, int64_t value) {
    if (sink_) {
        sink_->push_back(captured_int(code, value));
        return;
    }
    switch (group_value_type(code, record_)) {
        case GroupValueType::Int32:
            write_int32(code, static_cast<int32_t>(value));
            break;
        case GroupValueType::Int64:
            if (format_ == DxfFormat::Binary) write_typed(code, std::to_string(value));
            else write_string(code, aligned(value, 18));
            break;
        case GroupValueType::Bool:
            // A flag is 0 or 1; a number that fits the byte is written as it is (SUNSTUDY's hours are group 290), a value
            // that does not is a flag
            if (value < 0 || value > 255) value = value != 0 ? 1 : 0;
            if (format_ == DxfFormat::Binary) {
                write_code_binary(code);
                out_->put(static_cast<char>(value));
            } else {
                write_string(code, aligned(value, 6));
            }
            break;
        default:
            write_int16(code, static_cast<int>(value));
            break;
    }
}

void GroupWriter::write_typed(int code, const std::string& value) {
    if (sink_) {
        // Captured with its type, so the same value compares equal however it was given
        switch (group_value_type(code)) {
            case GroupValueType::Double:
                write_double(code, std::strtod(value.c_str(), nullptr));
                return;
            case GroupValueType::Int16:
            case GroupValueType::Int32:
            case GroupValueType::Int64:
            case GroupValueType::Bool:
                write_int(code, std::strtoll(value.c_str(), nullptr, 10));
                return;
            default:
                write_string(code, value);
                return;
        }
    }
    if (format_ != DxfFormat::Binary) {
        switch (group_value_type(code, record_)) {
            case GroupValueType::Int16:
            case GroupValueType::Int32:
            case GroupValueType::Int64:
            case GroupValueType::Bool:
                write_int(code, std::strtoll(value.c_str(), nullptr, 10));     // aligned
                return;
            default:
                write_string(code, value);
                return;
        }
    }

    const char* v = value.c_str();
    switch (group_value_type(code, record_)) {
        case GroupValueType::String:
        case GroupValueType::Unknown:   // no binary type known — kept as text
            write_string(code, value);
            break;
        case GroupValueType::Double:
            if (std::strncmp(v, "nan(0x", 6) == 0) {
                // A NaN as it was read (see read_binary)
                uint64_t bits = std::strtoull(v + 6, nullptr, 16);
                double nan_value;
                std::memcpy(&nan_value, &bits, 8);
                write_double(code, nan_value);
            } else {
                write_double(code, std::strtod(v, nullptr));
            }
            break;
        case GroupValueType::Int16:
            write_int16(code, static_cast<int>(std::strtol(v, nullptr, 10)));
            break;
        case GroupValueType::Int32:
            write_int32(code, static_cast<int32_t>(std::strtol(v, nullptr, 10)));
            break;
        case GroupValueType::Int64: {
            write_code_binary(code);
            int64_t i = std::strtoll(v, nullptr, 10);
            char buf[8];
            std::memcpy(buf, &i, 8);
            out_->write(buf, 8);
            break;
        }
        case GroupValueType::Bool:
            write_code_binary(code);
            {
                long flag = std::strtol(v, nullptr, 10);
                if (flag < 0 || flag > 255) flag = flag != 0 ? 1 : 0;
                out_->put(static_cast<char>(flag));
            }
            break;
        case GroupValueType::BinaryChunk: {
            // Length byte + raw bytes, converted back from the hex text. At most 127 bytes
            // per group, as AutoCAD writes binary DXF (R12 reference: "up to 127 bytes";
            // TrueView rejects 128): a longer chunk (a longer line in an ASCII file)
            // becomes several groups with the same code.
            size_t bytes = value.size() / 2;
            size_t at = 0;
            do {
                size_t len = std::min<size_t>(127, bytes - at);
                write_code_binary(code);
                out_->put(static_cast<char>(len));
                for (size_t j = at; j < at + len; ++j) {
                    char h[3] = { value[j * 2], value[j * 2 + 1], '\0' };
                    out_->put(static_cast<char>(std::strtoul(h, nullptr, 16)));
                }
                at += len;
            } while (at < bytes);
            break;
        }
    }
}

void GroupWriter::write_captured(const CapturedGroup& g) {
    switch (g.kind) {
        case CapturedGroup::Kind::String:
            if (group_value_type(g.code) == GroupValueType::BinaryChunk) write_typed(g.code, g.text);
            else write_string(g.code, g.text);
            break;
        case CapturedGroup::Kind::Int:
            write_int(g.code, g.i);
            break;
        case CapturedGroup::Kind::Double:
            write_double(g.code, g.d);
            break;
    }
}

std::string GroupWriter::convert_raw_text(const std::string& value) const {
    if (raw_code_page_ == 0 || raw_code_page_ == source_code_page_) return value;
    if (source_code_page_ == 0 || source_code_page_ == utf8_code_page) return value;
    bool high = false;
    for (char c : value) high = high || static_cast<uint8_t>(c) >= 0x80;
    if (!high) return value;                // ASCII and the escapes are the same in every page
    std::string utf8, error, converted;
    if (!decode_text(value, source_code_page_, "", utf8, error)) return value;
    if (!encode_text(utf8, raw_code_page_, converted)) return value;
    return converted;
}

void GroupWriter::write_raw(int code, const std::string& read_value, bool source_binary, int code_columns) {
    bool binary = format_ == DxfFormat::Binary;
    GroupValueType type = group_value_type(code, record_);
    note_record(code, read_value);
    bool text = type == GroupValueType::String || type == GroupValueType::Unknown;
    // A string is converted when $DWGCODEPAGE has changed since it was read
    std::string value = text ? convert_raw_text(read_value) : read_value;
    int caret_page = value != read_value ? raw_code_page_ : source_code_page_;

    if (!binary) {
        // ASCII: the file's text, as it was; from a binary file, strings get carets and
        // numbers AutoCAD's format (they are held as round-trip text)
        std::string line = value;
        if (source_binary && text) line = encode_carets(value, caret_page);
        if (source_binary && type == GroupValueType::Double) {
            line = format_double(std::strtod(value.c_str(), nullptr), number_style_);
        }
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%*d", code_columns > 0 ? code_columns : (plain_ ? 1 : 3), code);
        *out_ << buf << "\n" << line << "\n";
        return;
    }
    if (text) {
        // The bytes as they were (from ASCII: without the carets), not encoded again
        std::string bytes = source_binary ? value : decode_carets(value, caret_page);
        write_code_binary(code);
        out_->write(bytes.c_str(), bytes.size() + 1);
        return;
    }
    if (type == GroupValueType::BinaryChunk && source_binary && value.size() / 2 <= 255) {
        // A chunk read from a binary file is written as long as it was: AutoCAD 2025's own binary files have chunks of
        // 140 bytes in an XRECORD (the length is one byte), and a split at 127 would add a group header
        const size_t bytes = value.size() / 2;
        write_code_binary(code);
        out_->put(static_cast<char>(bytes));
        for (size_t j = 0; j < bytes; ++j) {
            char h[3] = { value[j * 2], value[j * 2 + 1], '\0' };
            out_->put(static_cast<char>(std::strtoul(h, nullptr, 16)));
        }
        return;
    }
    write_typed(code, value);        // numbers and binary chunks from their text
}

} // namespace internal
} // namespace hdxf
