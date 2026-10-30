#include "sat_writer.hpp"

#include "format.hpp"

namespace hdxf::acis::detail {

// ---------------------------------------------------------------------------
// Tokens
// ---------------------------------------------------------------------------

RecordWriter& RecordWriter::pointer(int record) {
    tokens_.push_back("$" + integer_text(record < 0 ? -1 : record));
    return *this;
}

RecordWriter& RecordWriter::integer(long long value) {
    tokens_.push_back(integer_text(value));
    return *this;
}

RecordWriter& RecordWriter::real(double value) {
    tokens_.push_back(real_text(value));
    return *this;
}

RecordWriter& RecordWriter::triple(const Point3& value) {
    real(value.x);
    real(value.y);
    real(value.z);
    return *this;
}

RecordWriter& RecordWriter::word(const std::string& value) {
    tokens_.push_back(value);
    return *this;
}

// "@7 unknown": the count of the characters, a space, the characters. One token, so that a
// line break never comes between the count and the text.
RecordWriter& RecordWriter::string(const std::string& value) {
    tokens_.push_back("@" + integer_text(static_cast<long long>(value.size())) + " " + value);
    return *this;
}

RecordWriter& RecordWriter::open() {
    tokens_.push_back("{");
    return *this;
}

RecordWriter& RecordWriter::close() {
    tokens_.push_back("}");
    return *this;
}

RecordWriter& RecordWriter::infinite_interval(int ends) {
    for (int i = 0; i < ends; ++i) tokens_.push_back("I");
    return *this;
}

RecordWriter entity_record(const std::string& type) {
    RecordWriter out(type);
    out.pointer(-1).integer(-1).pointer(-1);
    return out;
}

std::vector<std::string> RecordWriter::finish() const {
    std::vector<std::string> out = tokens_;
    out.push_back("#");
    return out;
}

// ---------------------------------------------------------------------------
// Lines
// ---------------------------------------------------------------------------

std::vector<std::string> layout_lines(const std::vector<Record>& records) {
    std::vector<std::string> lines;
    for (const Record& record : records) {
        std::string line;
        for (const std::string& token : record) {
            if (line.empty()) {
                line = token;
            } else if (line.size() + 1 + token.size() > kMaxLineLength) {
                lines.push_back(line);
                line = " " + token;
            } else {
                line += " " + token;
            }
        }
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

// "21500 <records> 2 24" padded to 23 characters, the product line, and the unit, resabs and
// resnor, each followed by a space. The entity count is 2 in every body of the facit: the asmheader
// and the body.
std::vector<std::string> header_lines(int record_count, const BuildOptions& options) {
    std::vector<std::string> lines;
    std::string first = "21500 " + integer_text(record_count) + " 2 24";
    if (first.size() < 23) first.append(23 - first.size(), ' ');
    lines.push_back(first);
    // Three counted strings: the product, the kernel, and the date (empty: "0" and its space)
    lines.push_back(integer_text(static_cast<long long>(std::string(kProductName).size())) + " " + kProductName + " " +
                    integer_text(static_cast<long long>(std::string(kKernelName).size())) + " " + kKernelName +
                    " 0  ");
    lines.push_back(real_text(options.header_unit) + " " + real_text(options.resabs) + " " +
                    real_text(options.resnor) + " ");
    return lines;
}

// ---------------------------------------------------------------------------
// The cipher
// ---------------------------------------------------------------------------

Result<std::vector<std::string>> cipher_lines(const std::vector<std::string>& lines) {
    std::vector<std::string> out;
    out.reserve(lines.size());
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string line;
        line.reserve(lines[i].size());
        for (char c : lines[i]) {
            unsigned char u = static_cast<unsigned char>(c);
            if (u == ' ') {
                line += c;
            } else if (u > 32 && u < 127) {
                line += static_cast<char>(159 - u);
            } else {
                return make_diagnostic(ErrorCode::Encoding, "line[" + integer_text(static_cast<long long>(i)) + "]", {},
                                       "SAT line " + integer_text(static_cast<long long>(i)) +
                                           " has a character the SAT cipher does not hold (code " +
                                           integer_text(u) + "); the SAT is printable ASCII");
            }
        }
        out.push_back(std::move(line));
    }
    return out;
}

} // namespace hdxf::acis::detail
