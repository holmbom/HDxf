#pragma once

#include "hdxf.hpp"
#include "group_codes.hpp"
#include <cmath>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace hdxf {
namespace internal {

// Group code width in binary DXF. R12 uses one byte (255 escapes to a 2-byte code),
// R13 and later always two bytes, little-endian.
enum class BinaryCodeWidth {
    OneByte,
    TwoBytes
};

inline BinaryCodeWidth binary_code_width(DxfVersion version) {
    return version == DxfVersion::R12 ? BinaryCodeWidth::OneByte : BinaryCodeWidth::TwoBytes;
}

// Reads groups from ASCII or binary DXF. Values come back as text, the way ASCII DXF
// stores them: numbers as decimal text, binary chunks (310-319, 1004) as upper-case hex.
class GroupReader {
public:
    explicit GroupReader(std::istream& in);

    // Looks for the binary sentinel. The version is not known yet, so the code width is
    // taken from the first group, 0/SECTION: R12 gives 00 'S' …, R13 and later 00 00 'S' ….
    // Leaves the stream at the first group.
    void detect_format();

    bool binary() const { return binary_; }
    BinaryCodeWidth code_width() const { return width_; }

    // Next group, comments (999) included, the value as written (only the line ending
    // removed). False at the end of the input, or on an error, which error() describes.
    bool read(GroupCode& gc);
    const std::string& error() const { return error_; }

    // Number of groups read so far — a position for error messages
    size_t count() const { return count_; }

private:
    bool read_ascii(GroupCode& gc);
    bool read_binary(GroupCode& gc);

    std::istream& in_;
    bool binary_ = false;
    BinaryCodeWidth width_ = BinaryCodeWidth::OneByte;
    std::string error_;
    size_t count_ = 0;
    std::string record_;        // the last group 0's value: some codes' widths depend on it
};

// How doubles are written in ASCII, as AutoCAD writes them. R12: 16 decimal places
// ("%.16f", trailing zeros dropped: "183.4975228948667052", "1.0"). AutoCad (2000+): up
// to 16 significant digits, at least one decimal, never more than 16 decimals — below 1e-15
// too ("-0.0000000000000004", "-0.0"; 4 274 such values in AutoCAD's files, HATCH, MTEXT …).
// AutoCadExponent: an MLINE's directions, which AutoCAD writes with "%.15E" below 1e-15
// ("1.224646799147353E-16", ACadSharp's samples — the only exponents below 1 in AutoCAD's
// files). All use "%.15E" from 1e16.
enum class NumberStyle {
    R12,
    AutoCad,
    AutoCadExponent
};

std::string format_double(double value, NumberStyle style);

// A group as the writer gives it, before it is encoded: the value with its type. Two
// captured groups are equal when code, type and value are.
struct CapturedGroup {
    enum class Kind { String, Int, Double };
    int code = 0;
    Kind kind = Kind::String;
    std::string text;
    int64_t i = 0;
    double d = 0.0;

    bool operator==(const CapturedGroup& o) const {
        if (code != o.code || kind != o.kind) return false;
        switch (kind) {
            case Kind::String: return text == o.text;
            case Kind::Int:    return i == o.i;
            case Kind::Double: return d == o.d || (std::isnan(d) && std::isnan(o.d));
        }
        return false;
    }
};

// Writes groups as ASCII or binary DXF, the code width chosen by the caller.
class GroupWriter {
public:
    GroupWriter(std::ostream& out, DxfFormat format, BinaryCodeWidth width);

    void write_sentinel();

    void set_number_style(NumberStyle style) { number_style_ = style; }

    // Code page for strings (plan B1): UTF-8 in, $DWGCODEPAGE bytes out; characters the
    // code page lacks as \U+XXXX. 0 = an unsupported code page: only ASCII as bytes.
    void set_code_page(int code_page) { code_page_ = code_page; }
    // The code page of the file a template was read from (its carets are undone in it)
    void set_source_code_page(int code_page) { source_code_page_ = code_page; }
    // The code page the preserved strings are written in, when it is not the one they were
    // read in ($DWGCODEPAGE changed since): their bytes are converted. 0 = the source's.
    void set_raw_code_page(int code_page) { raw_code_page_ = code_page; }

    // The first string that was not valid UTF-8, if any — the output is then not usable
    const std::string& error() const { return error_; }

    void write_string(int code, const std::string& value);
    void write_int16(int code, int value);
    void write_int32(int code, int32_t value);
    void write_double(int code, double value);
    // An integer written with the type its group code has (int16, int32, int64, bool)
    void write_int(int code, int64_t value);

    // A value held as text (unknown entities, XDATA, …) is written with the type its
    // group code has. ASCII keeps the text exactly as it is.
    void write_typed(int code, const std::string& value);

    // While a sink is set, groups go there instead of to the stream (nullptr: stream)
    void capture(std::vector<CapturedGroup>* sink) { sink_ = sink; }
    std::vector<CapturedGroup>* capturing() const { return sink_; }
    void write_captured(const CapturedGroup& g);

    // A group of a preserved record (RawRecord): its value as the file had it, read from
    // ASCII or binary (source_binary). In the same format it is written as it was — no
    // encoding, no carets; into the other format, carets and numbers are converted.
    // `code_columns`: the width of the code line as the record had it (ASCII, GroupCode::code_columns;
    // 0 = the writer's)
    void write_raw(int code, const std::string& value, bool source_binary, int code_columns = 0);

    // ASCII: code lines and integers without padding ("70", "8"), as some R13/R14 files have them,
    // instead of AutoCAD's three columns and right-aligned integers (" 70", "     8")
    void set_plain_ascii(bool plain) { plain_ = plain; }

    void set_stream(std::ostream& out) { out_ = &out; }

private:
    void write_code_binary(int code);
    // A preserved string's bytes in raw_code_page_ instead of source_code_page_ (unchanged
    // when the pages are the same or unknown, or the string has no byte above 0x7F)
    std::string convert_raw_text(const std::string& value) const;
    // An ASCII integer right-aligned in `width` characters (not aligned in plain mode)
    std::string aligned(long long value, int width) const;
    // Keeps the record type of group 0, for the codes whose type depends on it
    void note_record(int code, const std::string& value) { if (code == 0) record_ = value; }

    std::ostream* out_;
    std::vector<CapturedGroup>* sink_ = nullptr;
    DxfFormat format_;
    BinaryCodeWidth width_;
    NumberStyle number_style_ = NumberStyle::R12;
    bool plain_ = false;
    int code_page_ = 1252;
    int source_code_page_ = 1252;
    int raw_code_page_ = 0;
    std::string error_;
    std::string record_;        // the last group 0 written
};

} // namespace internal
} // namespace hdxf
