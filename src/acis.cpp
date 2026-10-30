#include "acis.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>

namespace hdxf {
namespace internal {

// ---------------------------------------------------------------------------
// SAT text
// ---------------------------------------------------------------------------

namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// A whole token as a number (from_chars: never the locale's decimal comma)
bool parse_number(const std::string& s, double& v) {
    if (s.empty()) return false;
    const char* first = s.data();
    const char* last = s.data() + s.size();
    if (*first == '+') ++first;
    auto r = std::from_chars(first, last, v);
    return r.ec == std::errc() && r.ptr == last;
}

bool parse_int(const std::string& s, long long& v) {
    if (s.empty()) return false;
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}

// Records the subtype definition that starts at token `index` of record `record`, in
// the order of the file ("ref n" counts them so)
void note_subtype(AcisFile& out, size_t record, size_t index) {
    out.subtypes.push_back({record, index});
}

// Takes back the subtype noted for a "{" that turned out to be "{ ref n }"
void drop_reference(AcisFile& out, size_t record, size_t index) {
    if (!out.subtypes.empty() && out.subtypes.back().record == record &&
        out.subtypes.back().token == index) {
        out.subtypes.pop_back();
    }
}

// One whitespace-free piece of SAT text as a token: "{", "}", $n, a number or a word.
// A "{" is noted as a subtype definition (taken back when "ref" follows).
AcisToken sat_token(AcisFile& out, size_t record, AcisRecord& r, const std::string& tok) {
    AcisToken t;
    double v = 0.0;
    if (tok == "{") {
        t.type = AcisTokenType::SubtypeStart;
        note_subtype(out, record, r.tokens.size());
    } else if (tok == "}") {
        t.type = AcisTokenType::SubtypeEnd;
    } else if (tok.size() > 1 && tok[0] == '$') {
        long long n = 0;
        if (parse_int(tok.substr(1), n)) {
            t.type = AcisTokenType::Pointer;
            t.number = static_cast<double>(n);
        } else {
            t.type = AcisTokenType::Word;
            t.text = tok;
        }
    } else if (parse_number(tok, v)) {
        t.number = v;
        t.text = tok;               // an integer field keeps every digit (SAB's 64-bit integers)
    } else {
        t.type = AcisTokenType::Word;
        t.text = tok;
        if (tok == "ref" && !r.tokens.empty() && r.tokens.back().type == AcisTokenType::SubtypeStart) {
            drop_reference(out, record, r.tokens.size() - 1);
        }
    }
    return t;
}

} // namespace

// Header: "version records entities flags", then (from 2.0, FILEINFO_VERSION) the product
// line and the units line. Records: an optional sequence number "-n", the save
// identifier, data, "#". "End-of-…" (or the history section's "Begin-of-…") ends them.
bool read_sat(const std::string& text, AcisFile& out) {
    out = AcisFile{};
    size_t pos = 0;
    auto line_end = [&](size_t from) {
        size_t e = text.find('\n', from);
        return e == std::string::npos ? text.size() : e;
    };
    // The first line
    size_t e1 = line_end(0);
    {
        std::string first = text.substr(0, e1);
        size_t a = first.find_first_not_of(" \t\r");
        if (a == std::string::npos) return false;
        size_t b = first.find_first_of(" \t\r", a);
        long long version = 0;
        if (!parse_int(first.substr(a, b == std::string::npos ? std::string::npos : b - a), version)) {
            return false;
        }
        out.version = static_cast<int>(version);
    }
    pos = e1 < text.size() ? e1 + 1 : text.size();
    if (out.version >= 200) {
        for (int i = 0; i < 2 && pos < text.size(); ++i) {
            size_t e = line_end(pos);
            pos = e < text.size() ? e + 1 : text.size();
        }
    }

    auto read_word = [&](std::string& w) {
        while (pos < text.size() && is_space(text[pos])) ++pos;
        if (pos >= text.size()) return false;
        size_t a = pos;
        while (pos < text.size() && !is_space(text[pos])) ++pos;
        w.assign(text, a, pos - a);
        return true;
    };

    std::string w;
    while (read_word(w)) {
        // The sequence number, when the file has them
        long long sequence = -1;
        if (w.size() > 1 && w[0] == '-' && std::isdigit(static_cast<unsigned char>(w[1]))) {
            long long n = 0;
            if (parse_int(w.substr(1), n)) {
                if (n >= static_cast<long long>(text.size())) return false;   // more records than characters
                sequence = n;
                if (!read_word(w)) break;
            }
        }
        if (w.rfind("End-of-", 0) == 0 || w.rfind("Begin-of-", 0) == 0) break;

        AcisRecord record;
        record.type = w;
        size_t index = sequence >= 0 ? static_cast<size_t>(sequence) : out.records.size();
        bool ended = false;
        while (true) {
            while (pos < text.size() && is_space(text[pos])) ++pos;
            if (pos >= text.size()) break;
            // A string from 7.0: @n, a space, n bytes
            if (text[pos] == '@') {
                size_t a = pos + 1;
                size_t b = a;
                while (b < text.size() && std::isdigit(static_cast<unsigned char>(text[b]))) ++b;
                long long n = 0;
                if (b > a && parse_int(text.substr(a, b - a), n)) {
                    size_t from = b < text.size() && text[b] == ' ' ? b + 1 : b;
                    size_t len = std::min(static_cast<size_t>(n), text.size() - from);
                    AcisToken t;
                    t.type = AcisTokenType::String;
                    t.text = text.substr(from, len);
                    t.begin = pos;
                    t.end = from + len;
                    record.tokens.push_back(t);
                    pos = from + len;
                    continue;
                }
            }
            // "#" ends the record wherever it stands: against the last value, or with the
            // next record's identifier right after it (AutoCAD writes "… #eye_refinement")
            if (text[pos] == '#') {
                record.end = pos;
                ++pos;
                ended = true;
                break;
            }
            size_t a = pos;
            while (pos < text.size() && !is_space(text[pos]) && text[pos] != '#') ++pos;
            std::string tok = text.substr(a, pos - a);
            AcisToken t = sat_token(out, index, record, tok);
            t.begin = a;
            t.end = pos;
            record.tokens.push_back(t);
        }
        if (!ended) break;                      // a record cut off: the data ends
        if (index >= out.records.size()) out.records.resize(index + 1);
        out.records[index] = std::move(record);
    }
    return !out.records.empty();
}

// ---------------------------------------------------------------------------
// SAB binary: the same records, each value after a tag byte (little-endian)
// ---------------------------------------------------------------------------

bool read_sab(const std::vector<uint8_t>& data, AcisFile& out) {
    out = AcisFile{};
    static const char* const magics[] = {"ACIS BinaryFile", "ASM BinaryFile4"};
    size_t pos = 0;
    bool known = false;
    for (const char* m : magics) {
        size_t n = std::strlen(m);
        if (data.size() >= n && std::memcmp(data.data(), m, n) == 0) {
            pos = n;
            known = true;
            break;
        }
    }
    if (!known) return false;
    auto have = [&](size_t n) { return pos + n <= data.size(); };
    auto i32 = [&]() {
        uint32_t u = static_cast<uint32_t>(data[pos]) | (static_cast<uint32_t>(data[pos + 1]) << 8) |
                     (static_cast<uint32_t>(data[pos + 2]) << 16) | (static_cast<uint32_t>(data[pos + 3]) << 24);
        pos += 4;
        return static_cast<int32_t>(u);
    };
    auto f64 = [&]() {
        uint64_t u = 0;
        for (int i = 7; i >= 0; --i) u = (u << 8) | data[pos + i];
        pos += 8;
        double d;
        std::memcpy(&d, &u, sizeof d);
        return d;
    };
    // version, records, entities, flags
    if (!have(16)) return false;
    out.version = i32();
    pos += 12;

    // One tagged value; false at the end of the data or on an unknown tag
    std::string name;                           // the record's identifier being built
    AcisRecord record;
    bool in_record = false;
    bool done = false;
    auto push_number = [&](double v) {
        AcisToken t;
        t.number = v;
        record.tokens.push_back(t);
    };
    while (!done && have(1)) {
        uint8_t tag = data[pos++];
        switch (tag) {
            case 2:
                if (!have(1)) return false;
                push_number(static_cast<int8_t>(data[pos]));
                pos += 1;
                break;
            case 3: {
                if (!have(2)) return false;
                int16_t s = static_cast<int16_t>(data[pos] | (data[pos + 1] << 8));
                pos += 2;
                push_number(s);
                break;
            }
            case 4:
                if (!have(4)) return false;
                push_number(i32());
                break;
            case 5: {
                if (!have(4)) return false;
                uint32_t u = static_cast<uint32_t>(i32());
                float f;
                std::memcpy(&f, &u, sizeof f);
                push_number(f);
                break;
            }
            case 6:
                if (!have(8)) return false;
                push_number(f64());
                break;
            case 7:
            case 8:
            case 9: {
                size_t width = tag == 7 ? 1 : (tag == 8 ? 2 : 4);
                if (!have(width)) return false;
                size_t n = 0;
                for (size_t i = 0; i < width; ++i) n |= static_cast<size_t>(data[pos + i]) << (8 * i);
                pos += width;
                if (!have(n)) return false;
                AcisToken t;
                t.type = AcisTokenType::String;
                t.text.assign(reinterpret_cast<const char*>(data.data() + pos), n);
                pos += n;
                record.tokens.push_back(t);
                break;
            }
            case 10:
            case 11: {
                AcisToken t;
                t.type = AcisTokenType::Logical;
                t.number = tag == 10 ? 1.0 : 0.0;
                record.tokens.push_back(t);
                break;
            }
            case 12: {
                if (!have(4)) return false;
                AcisToken t;
                t.type = AcisTokenType::Pointer;
                t.number = i32();
                record.tokens.push_back(t);
                break;
            }
            case 13:
            case 14: {
                // An identifier: the record's save identifier (14 joins its parts with
                // "-"), or an identifier in the data
                if (!have(1)) return false;
                size_t n = data[pos++];
                if (!have(n)) return false;
                std::string s(reinterpret_cast<const char*>(data.data() + pos), n);
                pos += n;
                if (!in_record) {
                    name += s;
                    if (tag == 14) {
                        name += '-';
                    } else {
                        if (name.rfind("End-of-", 0) == 0 || name.rfind("Begin-of-", 0) == 0) {
                            done = true;
                            break;
                        }
                        record = AcisRecord{};
                        record.type = name;
                        name.clear();
                        in_record = true;
                    }
                    break;
                }
                AcisToken t;
                t.type = AcisTokenType::Word;
                t.text = s;
                if (s == "ref" && !record.tokens.empty() &&
                    record.tokens.back().type == AcisTokenType::SubtypeStart) {
                    drop_reference(out, out.records.size(), record.tokens.size() - 1);
                }
                record.tokens.push_back(t);
                break;
            }
            case 15: {
                AcisToken t;
                t.type = AcisTokenType::SubtypeStart;
                if (in_record) note_subtype(out, out.records.size(), record.tokens.size());
                record.tokens.push_back(t);
                break;
            }
            case 16: {
                AcisToken t;
                t.type = AcisTokenType::SubtypeEnd;
                record.tokens.push_back(t);
                break;
            }
            case 17:
                if (in_record) out.records.push_back(std::move(record));
                record = AcisRecord{};
                in_record = false;
                break;
            case 18: {
                // Values written as SAT text (4-byte length): ODA's writer puts a record's
                // data so
                if (!have(4)) return false;
                size_t n = static_cast<uint32_t>(i32());
                if (!have(n)) return false;
                std::string text(reinterpret_cast<const char*>(data.data() + pos), n);
                pos += n;
                size_t a = 0;
                while (a < text.size()) {
                    while (a < text.size() && is_space(text[a])) ++a;
                    size_t b = a;
                    while (b < text.size() && !is_space(text[b])) ++b;
                    if (b > a) {
                        AcisToken t = sat_token(out, out.records.size(), record, text.substr(a, b - a));
                        record.tokens.push_back(t);
                    }
                    a = b;
                }
                break;
            }
            case 19:
            case 20:
                // A position, a vector: three numbers
                if (!have(24)) return false;
                for (int i = 0; i < 3; ++i) push_number(f64());
                break;
            case 21: {
                if (!have(4)) return false;
                AcisToken t;
                t.type = AcisTokenType::Enum;
                t.number = i32();
                record.tokens.push_back(t);
                break;
            }
            case 22:
                // A parameter position: two numbers
                if (!have(16)) return false;
                for (int i = 0; i < 2; ++i) push_number(f64());
                break;
            case 23: {
                // A 64-bit integer (ASM 223's attributes, before a record's end: AutoCAD's
                // 2013 files of surfaces and solids)
                if (!have(8)) return false;
                uint64_t u = 0;
                for (int i = 7; i >= 0; --i) u = (u << 8) | data[pos + i];
                pos += 8;
                // The number is a double for the geometry; the text keeps every digit (above
                // 2^53 too), as an integer of a SAT does
                push_number(static_cast<double>(static_cast<int64_t>(u)));
                record.tokens.back().text = std::to_string(static_cast<int64_t>(u));
                break;
            }
            default:
                return false;           // an unknown tag: the rest cannot be read
        }
        // Values before the first record (the header's strings and units) belong to no
        // record
        if (!in_record && tag != 13 && tag != 14 && tag != 17) record.tokens.clear();
    }
    return !out.records.empty();
}

std::string acis_deciphered(const AcisData& d) {
    std::string out;
    for (const auto& line : d.lines) {
        for (char c : line) {
            unsigned char u = static_cast<unsigned char>(c);
            out += (u > 32 && u < 159) ? static_cast<char>(159 - u) : c;
        }
        out += '\n';
    }
    return out;
}

bool acis_sat_version(const AcisData& d, int& version) {
    if (d.lines.empty()) return false;
    const std::string& line = d.lines.front();
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '	')) ++i;
    int value = 0;
    size_t digits = 0;
    for (; i < line.size(); ++i) {
        // The digits as AutoCAD enciphers them: 159 - c
        unsigned char c = static_cast<unsigned char>(line[i]);
        char plain = (c > 32 && c < 159) ? static_cast<char>(159 - c) : static_cast<char>(c);
        if (plain < '0' || plain > '9') break;
        if (digits >= 9) return false;
        value = value * 10 + (plain - '0');
        ++digits;
    }
    if (digits == 0) return false;
    // The number ends at a space or the end of the line
    if (i < line.size() && line[i] != ' ' && line[i] != '	') return false;
    version = value;
    return true;
}

int acis_newest_sat_version(DxfVersion version) {
    switch (version) {
        case DxfVersion::R13: return 106;   // format profile D11: R13c4 and R14 save ACIS 1.6 (106)
        case DxfVersion::R14: return 106;
        default:              return 0;
    }
}

std::string acis_target_error(const Drawing& dwg, DxfVersion target, const Drawing* original) {
    const int newest = acis_newest_sat_version(target);
    if (newest == 0) return std::string();
    const std::string name = target == DxfVersion::R13 ? "R13" : "R14";

    // The bodies of the file as it was read: their stream is kept as it is while it is the same
    std::unordered_map<std::string, const std::vector<std::string>*> read_lines;
    auto collect = [&](const std::vector<Entity>& list) {
        for (const auto& e : list) {
            if ((e.type == EntityType::Solid3D || e.type == EntityType::Region || e.type == EntityType::Body) &&
                !e.props.handle.empty()) {
                read_lines[e.props.handle] = &e.as_acis().lines;
            }
        }
    };
    if (original) {
        collect(original->entities());
        for (const auto& b : original->blocks()) collect(b.entities);
    }

    auto check = [&](const Entity& e) -> std::string {
        const char* what = e.type == EntityType::Solid3D ? "3DSOLID"
                         : e.type == EntityType::Region  ? "REGION"
                         : e.type == EntityType::Body    ? "BODY" : nullptr;
        if (!what) return std::string();
        const AcisData& d = e.as_acis();
        if (!d.lines.empty()) {
            auto same = read_lines.find(e.props.handle);
            bool unchanged = !e.props.handle.empty() && same != read_lines.end() && *same->second == d.lines;
            if (!unchanged) {
                int sat = 0;
                if (!acis_sat_version(d, sat)) {
                    return std::string("A ") + what + "'s ACIS data is not SAT and cannot be written as DXF " + name;
                }
                if (sat > newest || sat < 100) {   // 100 is ACIS 1.0, the first SAT
                    return "ACIS SAT version " + std::to_string(sat) + " cannot be written as DXF " + name;
                }
            }
        } else if (!d.sab.empty()) {
            return std::string("A ") + what + " with only SAB data cannot be written as DXF " + name;
        }
        if (!d.guid.empty()) return "A body's change GUID requires DXF 2000 or later";
        return std::string();
    };
    for (const auto& e : dwg.entities()) {
        std::string error = check(e);
        if (!error.empty()) return error;
    }
    for (const auto& b : dwg.blocks()) {
        for (const auto& e : b.entities) {
            std::string error = check(e);
            if (!error.empty()) return error;
        }
    }
    return std::string();
}

// ---------------------------------------------------------------------------
// Reading values: a cursor over a record's tokens that follows "{ ref n }" to the
// subtype it refers to
// ---------------------------------------------------------------------------

namespace {

class AcisReader {
public:
    AcisReader(const AcisFile& file, size_t record, size_t pos, int depth = 0)
        : file_(&file), record_(record), pos_(pos), depth_(depth) {}

    const AcisToken* peek(size_t ahead = 0) const {
        const auto& t = file_->records[record_].tokens;
        return pos_ + ahead < t.size() ? &t[pos_ + ahead] : nullptr;
    }
    const AcisToken* next() {
        const AcisToken* t = peek();
        if (t) ++pos_;
        return t;
    }
    bool number(double& v) {
        const AcisToken* t = peek();
        if (!t || (t->type != AcisTokenType::Number && t->type != AcisTokenType::Enum)) return false;
        v = t->number;
        ++pos_;
        return true;
    }
    bool point(Point3& p) { return number(p.x) && number(p.y) && number(p.z); }
    bool word(std::string& w) {
        const AcisToken* t = peek();
        if (!t || t->type != AcisTokenType::Word) return false;
        w = t->text;
        ++pos_;
        return true;
    }
    bool next_is_number() const {
        const AcisToken* t = peek();
        return t && t->type == AcisTokenType::Number;
    }
    // How many numbers follow
    size_t numbers_ahead() const {
        size_t n = 0;
        while (const AcisToken* t = peek(n)) {
            if (t->type != AcisTokenType::Number) break;
            ++n;
        }
        return n;
    }
    int version() const { return file_->version; }
    // A logical written as a word (true_word or "T"), as SAB's true/false, or (before 2.0)
    // as an integer
    bool flag(bool& v, const char* true_word) {
        const AcisToken* t = peek();
        if (!t) return false;
        if (t->type == AcisTokenType::Word) v = t->text == true_word || t->text == "T";
        else if (t->type == AcisTokenType::Logical || t->type == AcisTokenType::Number ||
                 t->type == AcisTokenType::Enum) v = t->number != 0.0;
        else return false;
        ++pos_;
        return true;
    }
    // An interval: "I" (infinite) or "F" and a number per end; SAB false / true and a
    // number
    bool interval() {
        double lo, hi;
        bool bounded;
        return interval(lo, hi, bounded);
    }
    // The same with its ends; bounded when both are finite
    bool interval(double& lo, double& hi, bool& bounded) {
        bounded = true;
        for (int end = 0; end < 2; ++end) {
            const AcisToken* t = peek();
            if (!t) return false;
            bool finite = false;
            if (t->type == AcisTokenType::Word) finite = t->text == "F";
            else if (t->type == AcisTokenType::Logical) finite = t->number != 0.0;
            else return false;
            ++pos_;
            if (finite) {
                double v;
                if (!number(v)) return false;
                (end == 0 ? lo : hi) = v;
            } else {
                bounded = false;
            }
        }
        return true;
    }
    bool next_is_interval() const {
        const AcisToken* t = peek();
        if (!t) return false;
        if (t->type == AcisTokenType::Logical) return true;
        return t->type == AcisTokenType::Word && (t->text == "I" || t->text == "F");
    }
    // At "{": a reader for the definition (the one referred to by "ref n"), and this one
    // past the matching "}"
    bool subtype(AcisReader& inside) {
        const AcisToken* t = peek();
        if (!t || t->type != AcisTokenType::SubtypeStart || depth_ >= 64) return false;
        const AcisToken* r = peek(1);
        if (r && r->type == AcisTokenType::Word && r->text == "ref") {
            const AcisToken* n = peek(2);
            if (!n || n->type != AcisTokenType::Number) return false;
            size_t index = static_cast<size_t>(n->number);
            if (index >= file_->subtypes.size()) return false;
            const AcisSubtype& s = file_->subtypes[index];
            if (s.record >= file_->records.size()) return false;
            inside = AcisReader(*file_, s.record, s.token + 1, depth_ + 1);
        } else {
            inside = AcisReader(*file_, record_, pos_ + 1, depth_ + 1);
        }
        return skip_subtype();
    }
    // Past the subtype that starts here
    bool skip_subtype() {
        int depth = 0;
        while (const AcisToken* t = next()) {
            if (t->type == AcisTokenType::SubtypeStart) ++depth;
            else if (t->type == AcisTokenType::SubtypeEnd && --depth == 0) return true;
        }
        return false;
    }

private:
    const AcisFile* file_;
    size_t record_;
    size_t pos_;
    int depth_;                         // subtypes inside subtypes: a broken file's refs may loop
};

// The parts of a save identifier: "plane-surface" → {"plane", "surface"}
std::vector<std::string> identifier_parts(const std::string& type) {
    std::vector<std::string> parts;
    size_t a = 0;
    while (true) {
        size_t b = type.find('-', a);
        parts.push_back(type.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return parts;
}

// The class the record belongs to (the identifier's last part) and the one derived from
// it ("plane" in "plane-surface")
std::string base_class(const AcisRecord& r) { return identifier_parts(r.type).back(); }

std::string derived_class(const AcisRecord& r) {
    auto parts = identifier_parts(r.type);
    return parts.size() >= 2 ? parts[parts.size() - 2] : std::string();
}

// The index of the first value after the ENTITY data: the attribute pointer, from 7.0
// the entity id (ENTITY_TAGS_VERSION), and for topology and geometry from 7.0 the
// pattern pointer (PATTERN_VERSION)
size_t data_start(const AcisFile& file, const AcisRecord& r) {
    size_t i = 1;
    if (file.version >= 700) {
        ++i;
        static const char* const patterned[] = {"body", "lump", "shell", "subshell", "face",
                                                "loop", "coedge", "edge", "vertex", "wire",
                                                "point", "curve", "surface", "pcurve"};
        std::string base = base_class(r);
        for (const char* p : patterned) {
            if (base == p) {
                ++i;
                break;
            }
        }
    }
    return i;
}

// The pointers of a record's data, in order
std::vector<int> pointers(const AcisFile& file, const AcisRecord& r) {
    std::vector<int> out;
    for (size_t i = data_start(file, r); i < r.tokens.size(); ++i) {
        if (r.tokens[i].type == AcisTokenType::Pointer) out.push_back(static_cast<int>(r.tokens[i].number));
    }
    return out;
}

// The index of the token after the k-th pointer of the data
size_t after_pointer(const AcisFile& file, const AcisRecord& r, size_t k) {
    size_t seen = 0;
    for (size_t i = data_start(file, r); i < r.tokens.size(); ++i) {
        if (r.tokens[i].type == AcisTokenType::Pointer && seen++ == k) return i + 1;
    }
    return r.tokens.size();
}

// A sense: "reversed", true, or (before 2.2) 1
bool reversed_at(const AcisRecord& r, size_t i) {
    if (i >= r.tokens.size()) return false;
    const AcisToken& t = r.tokens[i];
    if (t.type == AcisTokenType::Word) return t.text == "reversed";
    if (t.type == AcisTokenType::Logical || t.type == AcisTokenType::Number) return t.number != 0.0;
    return false;
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

bool is_bs3_keyword(const AcisToken* t) {
    return t && t->type == AcisTokenType::Word &&
           (t->text == "nubs" || t->text == "nurbs" || t->text == "nullbs");
}

// Knots as ACIS saves them (value, multiplicity) → the whole knot vector
bool read_knots(AcisReader& r, size_t count, std::vector<double>& knots) {
    std::vector<double> inner;
    for (size_t i = 0; i < count; ++i) {
        double v = 0.0;
        double mult = 0.0;
        if (!r.number(v) || !r.number(mult) || mult < 1.0 || mult > 64.0) return false;
        for (int k = 0; k < static_cast<int>(mult); ++k) inner.push_back(v);
    }
    if (inner.empty()) return false;
    knots.clear();
    knots.push_back(inner.front());
    knots.insert(knots.end(), inner.begin(), inner.end());
    knots.push_back(inner.back());
    return true;
}

bool closure_periodic(const AcisToken* t) {
    if (!t) return false;
    if (t->type == AcisTokenType::Word) return t->text == "periodic";
    return t->number == 2.0;
}

// bs3_curve: nubs | nurbs, degree, closure, the number of knots, the knots, the control
// points (with weights for nurbs). False for nullbs or anything else.
bool read_bs3_curve(AcisReader& r, AcisSplineCurve& c) {
    std::string kind;
    if (!r.word(kind) || (kind != "nubs" && kind != "nurbs")) return false;
    double degree = 0.0;
    double count = 0.0;
    if (!r.number(degree) || degree < 1.0 || degree > 32.0) return false;
    const AcisToken* closure = r.next();
    if (!r.number(count) || count < 2.0) return false;
    c.degree = static_cast<int>(degree);
    c.periodic = closure_periodic(closure);
    if (!read_knots(r, static_cast<size_t>(count), c.knots)) return false;
    if (c.knots.size() < static_cast<size_t>(2 * c.degree + 2)) return false;
    size_t n = c.knots.size() - c.degree - 1;
    bool rational = kind == "nurbs";
    for (size_t i = 0; i < n; ++i) {
        Point3 p;
        if (!r.point(p)) return false;
        c.points.push_back(p);
        if (rational) {
            double w = 1.0;
            if (!r.number(w)) return false;
            c.weights.push_back(w);
        }
    }
    return true;
}

// bs3_surface: nubs | nurbs, the degrees, (nurbs: which directions are rational),
// closures, singularities, the numbers of knots, u knots, v knots, the control points
// with u varying fastest
bool read_bs3_surface(AcisReader& r, AcisSplineSurface& s) {
    std::string kind;
    if (!r.word(kind) || (kind != "nubs" && kind != "nurbs")) return false;
    double du = 0.0;
    double dv = 0.0;
    if (!r.number(du) || !r.number(dv) || du < 1.0 || dv < 1.0 || du > 32.0 || dv > 32.0) return false;
    std::vector<const AcisToken*> words;
    while (const AcisToken* t = r.peek()) {
        if (t->type == AcisTokenType::Number || words.size() >= 5) break;
        words.push_back(r.next());
    }
    if (words.size() < 4) return false;
    size_t first = words.size() - 4;                    // a rational word before them
    s.periodic_u = closure_periodic(words[first]);
    s.periodic_v = closure_periodic(words[first + 1]);
    double nu = 0.0;
    double nv = 0.0;
    if (!r.number(nu) || !r.number(nv) || nu < 2.0 || nv < 2.0) return false;
    s.degree_u = static_cast<int>(du);
    s.degree_v = static_cast<int>(dv);
    if (!read_knots(r, static_cast<size_t>(nu), s.knots_u)) return false;
    if (!read_knots(r, static_cast<size_t>(nv), s.knots_v)) return false;
    if (s.knots_u.size() < static_cast<size_t>(2 * s.degree_u + 2) ||
        s.knots_v.size() < static_cast<size_t>(2 * s.degree_v + 2)) return false;
    s.count_u = static_cast<int>(s.knots_u.size()) - s.degree_u - 1;
    s.count_v = static_cast<int>(s.knots_v.size()) - s.degree_v - 1;
    bool rational = kind == "nurbs";
    size_t n = static_cast<size_t>(s.count_u) * static_cast<size_t>(s.count_v);
    for (size_t i = 0; i < n; ++i) {
        Point3 p;
        if (!r.point(p)) return false;
        s.points.push_back(p);
        if (rational) {
            double w = 1.0;
            if (!r.number(w)) return false;
            s.weights.push_back(w);
        }
    }
    return true;
}

// Up to a subtype's approximating bs3 data: the first bs3 of the subtype itself (not of a
// subtype inside it, such as a profile curve) that is a surface's (nubs | nurbs and two
// degrees) or a curve's (one degree, or nullbs) as asked. An older subtype starts with it
// (exactsur nubs …); a newer one writes its own data first (cyl_spl_sur: the profile, the
// direction …; rbblnsur: its supports, with their own nullbs) and its approximation after.
// full: the approximation level before it ("full", SAB 0) — after "summary" or "none" the
// spline is not the approximation. False when the subtype has none.
bool to_bs3(AcisReader& r, bool surface, bool& full) {
    full = true;
    const AcisToken* previous = nullptr;
    while (const AcisToken* t = r.peek()) {
        if (is_bs3_keyword(t)) {
            const AcisToken* a = r.peek(1);
            const AcisToken* b = r.peek(2);
            bool numbers = a && b && a->type == AcisTokenType::Number && b->type == AcisTokenType::Number;
            bool fits = surface ? (t->text != "nullbs" && numbers) : (t->text == "nullbs" || !numbers);
            if (fits) {
                if (previous && previous->type == AcisTokenType::Word)
                    full = previous->text != "summary" && previous->text != "none";
                else if (previous && previous->type == AcisTokenType::Enum)
                    full = previous->number == 0.0;
                return true;
            }
        }
        if (t->type == AcisTokenType::SubtypeEnd) return false;
        if (t->type == AcisTokenType::SubtypeStart) {
            if (!r.skip_subtype()) return false;
            previous = nullptr;
            continue;
        }
        previous = t;
        r.next();
    }
    return false;
}

bool unit(Point3& v) {
    double l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (!(l > 0.0)) return false;
    v = {v.x / l, v.y / l, v.z / l};
    return true;
}

bool read_surface(AcisReader& r, const std::string& kind, AcisSurface& s);
bool read_curve(AcisReader& r, const std::string& kind, AcisCurve& c);

// A curve written inside a subtype: its identifier, its data, (when saved) its range
bool read_inline_curve(AcisReader& r, AcisCurve& c) {
    std::string kind;
    if (!r.word(kind) || !read_curve(r, kind, c)) return false;
    if (r.next_is_interval() && !r.interval(c.t0, c.t1, c.bounded)) return false;
    return true;
}

// The procedural surfaces defined by curves (SAT Save File Format, spl_sur): a surface of
// revolution (rot_spl_sur, saved as "rotsur") — its profile curve, the axis' root and
// direction — and a sum of two curves (sum_spl_sur, "sumsur") — the curves and the base
// point; ASM writes its version number after the identifier. Then the spl_sur data: the
// approximation level ("full", "summary", "none"; SAB 0, 1, 2), with "full" the bs3
// surface and its fit tolerance — its knots the parameter range — and with "none" the u
// and v ranges. u is the profile's parameter and v the angle (the right hand about the
// axis: speaker.sat's faces end at edges turned so), or the first and the second curve's.
bool read_curve_surface(AcisReader& r, const std::string& name, AcisSurface& s) {
    bool revolution = name == "rotsur" || name == "rot_spl_sur";
    if (r.next_is_number()) r.next();
    int count = revolution ? 1 : 2;
    for (int i = 0; i < count; ++i) {
        AcisCurve c;
        if (!read_inline_curve(r, c) || c.type == AcisCurveType::None) return false;
        s.curves.push_back(c);
    }
    if (!r.point(s.origin)) return false;
    if (revolution && (!r.point(s.axis) || !unit(s.axis))) return false;
    s.type = revolution ? AcisSurfaceType::Revolution : AcisSurfaceType::Sum;
    // A sum with a straight curve is an extrusion
    if (!revolution) {
        s.ruled = s.curves[0].type == AcisCurveType::Straight || s.curves[1].type == AcisCurveType::Straight;
    }
    // Before ACIS 5 (AutoCAD 2000's SAT 400) the u and v ranges follow the point, without a level of approximation
    if (r.next_is_interval()) {
        bool bounded_u = false, bounded_v = false;
        if (r.interval(s.u0, s.u1, bounded_u) && r.next_is_interval() && r.interval(s.v0, s.v1, bounded_v)) {
            s.has_range = bounded_u && bounded_v;
        }
        return true;
    }
    const AcisToken* level = r.peek();
    if (!level) return true;
    bool full = level->type == AcisTokenType::Word ? level->text == "full"
                                                    : level->type == AcisTokenType::Enum && level->number == 0.0;
    bool none = level->type == AcisTokenType::Word ? level->text == "none"
                                                    : level->type == AcisTokenType::Enum && level->number == 2.0;
    r.next();
    if (full) {
        AcisSplineSurface approximation;
        if (read_bs3_surface(r, approximation)) {
            s.u0 = approximation.knots_u[static_cast<size_t>(approximation.degree_u)];
            s.u1 = approximation.knots_u[static_cast<size_t>(approximation.count_u)];
            s.v0 = approximation.knots_v[static_cast<size_t>(approximation.degree_v)];
            s.v1 = approximation.knots_v[static_cast<size_t>(approximation.count_v)];
            s.has_range = true;
        }
    } else if (none) {
        bool bounded_u = false, bounded_v = false;
        if (r.next_is_interval() && r.interval(s.u0, s.u1, bounded_u) && r.next_is_interval() &&
            r.interval(s.v0, s.v1, bounded_v)) {
            s.has_range = bounded_u && bounded_v;
        }
    }
    return true;
}

// A spline surface's data: its sense, then the subtype — spl_sur data with the
// approximating bs3 surface and its fit tolerance (exactsur, exact_spl_sur: the surface
// itself). An extrusion (cyl_spl_sur) or a ruled surface is straight in one direction,
// so a coordinate has no extreme inside it; it needs no approximation. A surface of
// revolution and a sum of curves by their own definition.
bool read_spline_surface(AcisReader& r, AcisSurface& s) {
    bool reversed = false;
    if (!r.flag(reversed, "reversed")) return false;
    AcisReader inside = r;
    if (!r.subtype(inside)) return false;
    std::string name;
    if (!inside.word(name)) return false;
    if (name == "rotsur" || name == "rot_spl_sur" || name == "sumsur" || name == "sum_spl_sur") {
        AcisReader definition = inside;
        if (read_curve_surface(definition, name, s)) {
            s.reversed = reversed;
            if (r.next_is_interval()) {
                r.interval();
                r.interval();
            }
            return true;
        }
        s = AcisSurface{};
    }
    s.type = AcisSurfaceType::Spline;
    s.reversed = reversed;
    s.ruled = name == "cyl_spl_sur" || name == "ruled_spl_sur" || name == "ruled_tpr_spl_sur";
    bool full = true;
    bool have = to_bs3(inside, true, full) && full && read_bs3_surface(inside, s.spline);
    if (have) {
        double fit = 0.0;
        if (!inside.number(fit)) fit = 0.0;
        bool exact = name == "exactsur" || name == "exact_spl_sur";
        s.fit_tolerance = exact ? 0.0 : std::fabs(fit);
    } else {
        s.spline = AcisSplineSurface{};
        if (!s.ruled) return false;
    }
    if (r.next_is_interval()) {
        r.interval();
        r.interval();
    }
    return true;
}

// The end of an analytic surface's data: its parameter sense (a word, a logical or — before
// 2.0 — an integer) and (from 1.6) its u and v ranges. Read to the end so that a surface
// inside a subtype is followed by what comes after it.
bool finish_surface(AcisReader& r, bool* sense_out = nullptr, const char* true_word = "reverse_v") {
    bool sense = false;
    if (!r.flag(sense, true_word)) return false;
    if (sense_out) *sense_out = sense;
    for (int i = 0; i < 2; ++i) {
        if (r.next_is_interval() && !r.interval()) return false;
    }
    return true;
}

// A surface's data after its identifier: plane, cone, sphere, torus or spline
bool read_surface(AcisReader& r, const std::string& kind, AcisSurface& s) {
    s = AcisSurface{};
    if (kind == "plane") {
        // Root, normal, the u direction, the sense of v, the ranges
        if (!r.point(s.origin) || !r.point(s.axis) || !unit(s.axis)) return false;
        Point3 u;
        if (r.numbers_ahead() >= 3) {
            if (!r.point(u)) return false;
            s.u_direction = u;
        }
        s.type = AcisSurfaceType::Plane;
        return finish_surface(r, &s.reverse_v);
    }
    if (kind == "cone") {
        // The base ellipse (center, normal, major axis, ratio, from 1.6 its range), the
        // half angle's sine and cosine, from 4.0 (CONE_SCALING_VERSION) the u scale, the
        // sense of u, the ranges
        if (!r.point(s.origin) || !r.point(s.axis) || !r.point(s.major) || !r.number(s.ratio)) return false;
        if (!unit(s.axis)) return false;
        if (r.next_is_interval() && !r.interval()) return false;
        if (!r.number(s.sine) || !r.number(s.cosine)) return false;
        double scale = 0.0;
        if (r.version() >= 400 && !r.number(scale)) return false;
        s.u_scale = scale;
        s.type = AcisSurfaceType::Cone;
        return finish_surface(r, &s.reverse_u, "reversed");
    }
    if (kind == "sphere") {
        // Center, radius, then (when saved) the u direction and the pole
        if (!r.point(s.origin) || !r.number(s.radius)) return false;
        s.axis = {0.0, 0.0, 1.0};
        s.major = {1.0, 0.0, 0.0};
        if (r.numbers_ahead() >= 6) {
            if (!r.point(s.major) || !r.point(s.axis) || !unit(s.axis)) return false;
        }
        s.type = AcisSurfaceType::Sphere;
        return finish_surface(r, &s.reverse_v);
    }
    if (kind == "torus") {
        // Center, normal, major and minor radius, then (when saved) the u direction
        if (!r.point(s.origin) || !r.point(s.axis) || !unit(s.axis) || !r.number(s.radius) ||
            !r.number(s.minor)) return false;
        s.major = {1.0, 0.0, 0.0};
        if (r.numbers_ahead() >= 3 && !r.point(s.major)) return false;
        s.type = AcisSurfaceType::Torus;
        return finish_surface(r, &s.reverse_v);
    }
    if (kind == "spline") return read_spline_surface(r, s);
    return false;
}

// A surface written inside a subtype: its identifier, then its data
bool read_inline_surface(AcisReader& r, AcisSurface& s) {
    std::string kind;
    if (!r.word(kind)) return false;
    if (kind == "null_surface") {
        s = AcisSurface{};
        return true;
    }
    return read_surface(r, kind, s);
}

// An intcurve's data: its sense, then the subtype — int_cur data: the approximating bs3
// curve, its fit tolerance, the two surfaces (null_surface for an exact curve), …
bool read_intcurve(AcisReader& r, AcisCurve& c) {
    bool reversed = false;
    if (!r.flag(reversed, "reversed")) return false;
    AcisReader inside = r;
    if (!r.subtype(inside)) return false;
    std::string name;
    if (!inside.word(name)) return false;
    bool full = true;
    if (!to_bs3(inside, false, full) || !full) return false;
    if (!read_bs3_curve(inside, c.spline)) return false;
    c.type = AcisCurveType::Spline;
    c.reversed = reversed;
    c.exact = name == "exactcur" || name == "exact_int_cur";
    double fit = 0.0;
    if (!inside.number(fit)) return true;
    c.fit_tolerance = c.exact ? 0.0 : std::fabs(fit);
    if (c.fit_tolerance > 0.0) {
        AcisSurface s1;
        AcisSurface s2;
        if (read_inline_surface(inside, s1) && read_inline_surface(inside, s2)) {
            c.surface1 = s1;
            c.surface2 = s2;
        }
    }
    return true;
}

bool read_curve(AcisReader& r, const std::string& kind, AcisCurve& c) {
    c = AcisCurve{};
    if (kind == "straight") {
        if (!r.point(c.origin) || !r.point(c.direction)) return false;
        c.type = AcisCurveType::Straight;
        return true;
    }
    if (kind == "ellipse") {
        if (!r.point(c.origin) || !r.point(c.axis) || !r.point(c.major) || !r.number(c.ratio)) return false;
        if (!unit(c.axis)) return false;
        c.type = AcisCurveType::Ellipse;
        return true;
    }
    if (kind == "intcurve") return read_intcurve(r, c);
    return false;
}

// ---------------------------------------------------------------------------
// Topology → bodies
// ---------------------------------------------------------------------------

class BodyBuilder {
public:
    // Each record's class and pointers, and the topology by owner: lumps by body, shells
    // by lump, faces by shell, loops by face, coedges by loop or wire, wires by body or
    // shell
    explicit BodyBuilder(const AcisFile& file) : file_(file) {
        classes_.resize(file.records.size());
        pointers_.resize(file.records.size());
        for (size_t i = 0; i < file.records.size(); ++i) {
            const AcisRecord& r = file.records[i];
            if (r.tokens.empty() || r.tokens[0].type != AcisTokenType::Pointer) continue;
            classes_[i] = base_class(r);
            pointers_[i] = pointers(file, r);
            const std::string& c = classes_[i];
            const std::vector<int>& p = pointers_[i];
            int id = static_cast<int>(i);
            if (c == "lump" && p.size() >= 3) lumps_[p[2]].push_back(id);
            else if (c == "shell" && p.size() >= 4) shells_[p.size() >= 5 ? p[4] : p[3]].push_back(id);
            else if (c == "face" && p.size() >= 3) faces_[p[2]].push_back(id);
            else if (c == "loop" && p.size() >= 3) loops_[p[2]].push_back(id);
            else if (c == "coedge" && p.size() >= 5) coedges_[p[4]].push_back(id);
            else if (c == "wire" && p.size() >= 3) wires_[p[2]].push_back(id);
        }
    }

    std::vector<AcisBody> bodies() {
        std::vector<AcisBody> out;
        for (size_t i = 0; i < file_.records.size(); ++i) {
            if (classes_[i] == "body") out.push_back(body(static_cast<int>(i)));
        }
        return out;
    }

private:
    const AcisRecord* record(int i, const char* cls) const {
        if (i < 0 || static_cast<size_t>(i) >= file_.records.size()) return nullptr;
        if (classes_[static_cast<size_t>(i)] != cls) return nullptr;
        return &file_.records[static_cast<size_t>(i)];
    }
    const std::vector<int>& owned(const std::unordered_map<int, std::vector<int>>& by, int owner) const {
        static const std::vector<int> none;
        auto it = by.find(owner);
        return it == by.end() ? none : it->second;
    }

    AcisBody body(int index);
    void transform(int index, AcisBody& b);
    int edge(int index, AcisBody& b);
    int curve(int index, AcisBody& b);
    int surface(int index, AcisBody& b);
    bool vertex_point(int vertex, Point3& p) const;

    const AcisFile& file_;
    std::vector<std::string> classes_;
    std::vector<std::vector<int>> pointers_;
    std::unordered_map<int, std::vector<int>> lumps_;
    std::unordered_map<int, std::vector<int>> shells_;
    std::unordered_map<int, std::vector<int>> faces_;
    std::unordered_map<int, std::vector<int>> loops_;
    std::unordered_map<int, std::vector<int>> coedges_;
    std::unordered_map<int, std::vector<int>> wires_;
    std::unordered_map<int, int> edges_;
    std::unordered_map<int, int> curves_;
    std::unordered_map<int, int> surfaces_;
};

// The body's lumps → shells → faces → loops → coedges, found by their owner pointers (a
// face's owner is its shell also when it is in a subshell), and the wires of the body and
// its shells. A coedge: next, previous, partner, edge, its sense, its owner.
AcisBody BodyBuilder::body(int index) {
    AcisBody b;
    edges_.clear();
    curves_.clear();
    surfaces_.clear();
    const std::vector<int>& p = pointers_[static_cast<size_t>(index)];
    if (p.size() >= 3) transform(p[2], b);

    std::vector<int> shells;
    for (int lump : owned(lumps_, index)) {
        for (int shell : owned(shells_, lump)) shells.push_back(shell);
    }
    for (int shell : shells) {
        for (int f : owned(faces_, shell)) {
            const AcisRecord& fr = file_.records[static_cast<size_t>(f)];
            const std::vector<int>& fp = pointers_[static_cast<size_t>(f)];
            AcisFace face;
            if (fp.size() >= 5) face.surface = surface(fp[4], b);
            face.reversed = reversed_at(fr, after_pointer(file_, fr, 4));
            for (int l : owned(loops_, f)) {
                AcisLoop loop;
                for (int c : owned(coedges_, l)) {
                    const AcisRecord& cr = file_.records[static_cast<size_t>(c)];
                    AcisCoedge ce;
                    ce.edge = edge(pointers_[static_cast<size_t>(c)][3], b);
                    ce.reversed = reversed_at(cr, after_pointer(file_, cr, 3));
                    if (ce.edge >= 0) loop.coedges.push_back(ce);
                }
                face.loops.push_back(loop);
            }
            b.faces.push_back(face);
        }
    }
    std::vector<int> owners = shells;
    owners.push_back(index);
    for (int owner : owners) {
        for (int w : owned(wires_, owner)) {
            for (int c : owned(coedges_, w)) edge(pointers_[static_cast<size_t>(c)][3], b);
        }
    }
    return b;
}

// transform: the 3×3 matrix (rows), the translation, the scale. A position is
// p · matrix · scale + translation (row vectors, as ACIS's SPAtransf)
void BodyBuilder::transform(int index, AcisBody& b) {
    const AcisRecord* r = record(index, "transform");
    if (!r) return;
    AcisReader rd(file_, static_cast<size_t>(index), data_start(file_, *r));
    double a[3][3];
    Point3 t;
    double scale = 1.0;
    for (auto& row : a) {
        if (!rd.number(row[0]) || !rd.number(row[1]) || !rd.number(row[2])) return;
    }
    if (!rd.point(t)) return;
    if (!rd.number(scale)) scale = 1.0;
    for (int i = 0; i < 3; ++i) {
        for (int k = 0; k < 3; ++k) b.transform[i][k] = a[k][i] * scale;
    }
    b.transform[0][3] = t.x;
    b.transform[1][3] = t.y;
    b.transform[2][3] = t.z;
}

bool BodyBuilder::vertex_point(int vertex, Point3& p) const {
    if (!record(vertex, "vertex")) return false;
    const std::vector<int>& vp = pointers_[static_cast<size_t>(vertex)];
    if (vp.empty()) return false;
    const AcisRecord* pt = record(vp.back(), "point");
    if (!pt) return false;
    AcisReader rd(file_, static_cast<size_t>(vp.back()), data_start(file_, *pt));
    return rd.point(p);
}

// edge: start vertex, (from ACIS 5) its parameter, end vertex, its parameter, a coedge,
// the curve, the sense
int BodyBuilder::edge(int index, AcisBody& b) {
    auto found = edges_.find(index);
    if (found != edges_.end()) return found->second;
    const AcisRecord* r = record(index, "edge");
    if (!r) return -1;
    const std::vector<int>& p = pointers_[static_cast<size_t>(index)];
    if (p.size() < 4) return -1;
    AcisEdge e;
    e.has_start = vertex_point(p[0], e.start);
    e.has_end = vertex_point(p[1], e.end);
    e.closed = p[0] == p[1] && p[0] >= 0;
    e.curve = curve(p[3], b);
    e.reversed = reversed_at(*r, after_pointer(file_, *r, 3));
    int i = static_cast<int>(b.edges.size());
    b.edges.push_back(e);
    edges_[index] = i;
    return i;
}

int BodyBuilder::curve(int index, AcisBody& b) {
    auto found = curves_.find(index);
    if (found != curves_.end()) return found->second;
    const AcisRecord* r = record(index, "curve");
    if (!r) return curves_[index] = -1;
    AcisReader rd(file_, static_cast<size_t>(index), data_start(file_, *r));
    AcisCurve c;
    if (!read_curve(rd, derived_class(*r), c)) return curves_[index] = -1;
    b.curves.push_back(c);
    return curves_[index] = static_cast<int>(b.curves.size()) - 1;
}

int BodyBuilder::surface(int index, AcisBody& b) {
    auto found = surfaces_.find(index);
    if (found != surfaces_.end()) return found->second;
    const AcisRecord* r = record(index, "surface");
    if (!r) return surfaces_[index] = -1;
    AcisReader rd(file_, static_cast<size_t>(index), data_start(file_, *r));
    AcisSurface s;
    if (!read_surface(rd, derived_class(*r), s)) return surfaces_[index] = -1;
    b.surfaces.push_back(s);
    return surfaces_[index] = static_cast<int>(b.surfaces.size()) - 1;
}

} // namespace

std::vector<AcisBody> acis_bodies(const AcisFile& file) {
    BodyBuilder builder(file);
    return builder.bodies();
}

bool acis_record_curve(const AcisFile& file, size_t record, AcisCurve& c) {
    if (record >= file.records.size()) return false;
    const AcisRecord& r = file.records[record];
    std::string base = base_class(r);
    AcisReader rd(file, record, data_start(file, r));
    if (base != "curve" || !read_curve(rd, derived_class(r), c)) return false;
    // The range, when it is saved after the curve's data
    if (rd.next_is_interval() && !rd.interval(c.t0, c.t1, c.bounded)) c.bounded = false;
    return true;
}

bool acis_record_surface(const AcisFile& file, size_t record, AcisSurface& s) {
    if (record >= file.records.size()) return false;
    const AcisRecord& r = file.records[record];
    if (base_class(r) != "surface") return false;
    AcisReader rd(file, record, data_start(file, r));
    return read_surface(rd, derived_class(r), s);
}

bool acis_pcurve(const AcisFile& file, size_t record, AcisSplineCurve& uv, AcisSurface& surface, bool& reversed,
                 double offset[2]) {
    if (record >= file.records.size()) return false;
    const AcisRecord& r = file.records[record];
    if (base_class(r) != "pcurve") return false;
    AcisReader rd(file, record, data_start(file, r));
    double type = 0.0;
    if (!rd.number(type) || type != 0.0 || !rd.flag(reversed, "reversed")) return false;
    AcisReader inside = rd;
    if (!rd.subtype(inside)) return false;
    std::string name;
    if (!inside.word(name) || (name != "exp_par_cur" && name != "exppc")) return false;
    // The bs2 curve: as a bs3 curve's, two numbers a point
    std::string kind;
    double degree = 0.0, count = 0.0;
    if (!inside.word(kind) || (kind != "nubs" && kind != "nurbs")) return false;
    if (!inside.number(degree) || degree < 1.0 || degree > 32.0) return false;
    const AcisToken* closure = inside.next();
    if (!inside.number(count) || count < 2.0) return false;
    uv = AcisSplineCurve{};
    uv.degree = static_cast<int>(degree);
    uv.periodic = closure_periodic(closure);
    if (!read_knots(inside, static_cast<size_t>(count), uv.knots)) return false;
    if (uv.knots.size() < static_cast<size_t>(2 * uv.degree + 2)) return false;
    size_t n = uv.knots.size() - uv.degree - 1;
    for (size_t i = 0; i < n; ++i) {
        Point3 p;
        if (!inside.number(p.x) || !inside.number(p.y)) return false;
        uv.points.push_back(p);
        if (kind == "nurbs") {
            double w = 1.0;
            if (!inside.number(w)) return false;
            uv.weights.push_back(w);
        }
    }
    double fit = 0.0;
    if (!inside.number(fit) || !read_inline_surface(inside, surface)) return false;
    offset[0] = offset[1] = 0.0;
    if (rd.next_is_number() && !rd.number(offset[0])) return false;
    if (rd.next_is_number() && !rd.number(offset[1])) return false;
    return true;
}

bool acis_vertex_point(const AcisFile& file, size_t vertex, Point3& p) {
    if (vertex >= file.records.size()) return false;
    const AcisRecord& v = file.records[vertex];
    if (base_class(v) != "vertex") return false;
    std::vector<int> vp = pointers(file, v);
    // The point is the vertex's last pointer (a tolerant vertex: its tolerance after it)
    if (vp.empty() || vp.back() < 0 || static_cast<size_t>(vp.back()) >= file.records.size()) return false;
    const AcisRecord& pt = file.records[static_cast<size_t>(vp.back())];
    if (base_class(pt) != "point") return false;
    AcisReader rd(file, static_cast<size_t>(vp.back()), data_start(file, pt));
    return rd.point(p);
}

// ---------------------------------------------------------------------------
// Where an entity's ACIS data is
std::vector<AcisBody> AcisSource::bodies(const Entity& e) {
    bool surface = e.type == EntityType::Surface || e.type == EntityType::ExtrudedSurface ||
                   e.type == EntityType::LoftedSurface || e.type == EntityType::RevolvedSurface ||
                   e.type == EntityType::SweptSurface || e.type == EntityType::PlaneSurface ||
                   e.type == EntityType::NurbsSurface;
    if (e.type != EntityType::Solid3D && e.type != EntityType::Region && e.type != EntityType::Body && !surface) return {};
    const AcisData& d = surface ? e.as_surface().acis : e.as_acis();
    AcisFile file;
    if (!d.lines.empty()) {
        if (!read_sat(acis_deciphered(d), file)) return {};
        return acis_bodies(file);
    }
    if (d.sab.empty() || !read_sab(d.sab, file)) return {};
    return acis_bodies(file);
}

} // namespace internal
} // namespace hdxf
