#include "dxf_reader.hpp"
#include "ascii_form.hpp"
#include "mtext_data.hpp"
#include "header_defaults.hpp"
#include "dimstyle_legacy.hpp"
#include "mleader.hpp"
#include "viewport_mview.hpp"
#include "TypedRecordTypes.hpp"
#include "proxy_record.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>

namespace hdxf {
namespace internal {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

DxfReader::DxfReader(std::istream& in)
    : groups_(in)
{
}

// The SAT lines of AcDbModelerGeometry. AutoCAD's form (see parse_acis()): a line is its 3s
// and the 1 that ends it — its 1 is shorter than 255 characters. The reference's form: a 3
// goes on with the 1 before it when that 1 has 255 characters or more ("if previous group 1
// string is greater than 255 characters", 3DSOLID p. 82 of the 2014 reference; speaker.dxf,
// AC1015). 3s at the end without a 1 are a line of their own.
class AcisLineReader {
public:
    explicit AcisLineReader(AcisData& d) : d_(d) {}

    void continue_line(const std::string& part) {
        if (!has_pending_ && last_long_) {
            d_.lines.back() += part;
            return;
        }
        pending_ += part;
        has_pending_ = true;
    }
    // written: the 1's length as it stands in the file (a caret escape two characters: speaker.dxf's
    // "^I"), which is what the writer of the reference's form counted
    void end_line(const std::string& part, size_t written) {
        last_long_ = !has_pending_ && std::max(written, part.size()) >= 255;
        d_.lines.push_back(pending_ + part);
        pending_.clear();
        has_pending_ = false;
    }
    void finish() {
        if (has_pending_) end_line(std::string(), 0);
    }

private:
    AcisData& d_;
    std::string pending_;
    bool has_pending_ = false;
    bool last_long_ = false;            // the last line a 1 of 255 characters or more alone
};

static void trim_trailing_spaces(std::string& s) {
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
}

// An attribute's locked position as AutoCAD writes it up to 2004 (R12 too): XDATA AcDbAttr
// 1070 0 1070 1 (ACadSharp's sample_AC1009 … AC1018; 2007 has 280 1 there). A block of
// exactly that form becomes the field; any other stays in the XDATA.
static void take_attribute_lock(std::vector<XdataList>& xdata, bool& lock) {
    for (size_t i = 0; i < xdata.size(); ++i) {
        const XdataList& x = xdata[i];
        if (x.app_name != "AcDbAttr" || x.items.size() != 2) continue;
        if (x.items[0].code != 1070 || x.items[1].code != 1070) continue;
        if (std::atoi(x.items[0].value.c_str()) != 0) continue;
        lock = std::atoi(x.items[1].value.c_str()) != 0;
        xdata.erase(xdata.begin() + static_cast<long>(i));
        return;
    }
}

// ---------------------------------------------------------------------------
// Group code reading
// ---------------------------------------------------------------------------

bool DxfReader::read_group(GroupCode& gc) {
    if (has_pending_) {
        gc = pending_;
        has_pending_ = false;
        return true;
    }

    while (groups_.read(gc)) {
        // Skip comments (group 999) per DXF spec — they are not preserved either
        if (gc.code == 999) continue;

        if (capture_) capture(gc);
        if (gc.code == 0) note_marker(gc);

        // Record types (0) and header variable names (9) are keywords, never user
        // text, so padding from sloppy writers is dropped there — after capture,
        // which keeps the value as written
        if (gc.code == 0 || gc.code == 9) trim_trailing_spaces(gc.value);
        written_size_ = gc.value.size();

        if (r13_plus_ && !interpret(gc)) continue;

        // Strings to UTF-8 (plan B1). The record above keeps them as written. MTEXT's
        // text (1, 3) is decoded when its parts are joined.
        if (group_value_type(gc.code) == GroupValueType::String &&
            !(keep_text_encoded_ && (gc.code == 1 || gc.code == 3))) {
            if (!decode_string(gc.value, gc.code)) return false;
        }
        return true;
    }
    if (!groups_.error().empty()) error_ = groups_.error();
    return false;
}

// The width of the code line of a group 0 that ends a section or table, where it is not three columns
void DxfReader::note_marker(const GroupCode& gc) {
    std::string value = gc.value;
    trim_trailing_spaces(value);
    if (value == "ENDSEC") {
        if (gc.code_columns != 0) markers_["ENDSEC:" + section_] = gc.code_columns;
    } else if (value == "ENDTAB") {
        if (gc.code_columns != 0) markers_["ENDTAB#" + std::to_string(endtabs_)] = gc.code_columns;
        ++endtabs_;
    } else if (value == "EOF") {
        if (gc.code_columns != 0) markers_["EOF"] = gc.code_columns;
    }
}

// A string as in the file (code page or UTF-8, \U+XXXX, carets in ASCII) → UTF-8
bool DxfReader::decode_string(std::string& value, int code) {
    if (!needs_text_conversion(value)) return true;
    std::string text = groups_.binary() ? value : decode_carets(value, code_page_);
    std::string message;
    if (!decode_text(text, code_page_, code_page_name_, value, message)) {
        error_ = message + " (group code " + std::to_string(code) + ", group " +
                 std::to_string(groups_.count()) + " of the file).";
        return false;
    }
    return true;
}

// R13+: groups that belong to something else are captured but never given to the
// parsers — 102 groups ({ACAD_REACTORS, {ACAD_XDICTIONARY, {<app> … }) and an
// embedded object after 101 (its 10/40/1 would overwrite the entity's own), unless the
// parser takes embedded objects (MTEXT, ATTRIB, ATTDEF: want_embedded_). The current
// subclass (100) is kept for codes whose meaning depends on it.
bool DxfReader::interpret(const GroupCode& gc) {
    if (gc.code == 0) {
        in_102_ = false;
        in_embedded_ = false;
        subclass_.clear();
        return true;
    }
    if (gc.code == 102) {
        in_102_ = !gc.value.empty() && gc.value[0] == '{';
        return false;
    }
    if (in_102_) return false;
    if (in_embedded_) {
        if (gc.code < 1000) return want_embedded_;
        in_embedded_ = false;                  // XDATA follows the embedded object
    }
    if (gc.code == 101) {
        in_embedded_ = true;
        return want_embedded_;
    }
    if (gc.code == 100) subclass_ = gc.value;
    return true;
}

// ---------------------------------------------------------------------------
// Record capture (DXF 2000+): every fresh group goes into the open record; a code-0
// group closes it. The parser that owns a record takes it with take_record() right
// after reading the next code 0. A record nobody takes is kept as a stray, unless it is
// only a marker (SECTION + name, ENDSEC, ENDTAB, EOF).
// ---------------------------------------------------------------------------

static bool is_marker(const RawRecord& record) {
    std::vector<DxfGroup> groups = record.groups();
    if (groups.empty() || groups[0].code != 0) return false;
    std::string type = groups[0].value;
    trim_trailing_spaces(type);
    if (groups.size() == 1) return type == "ENDSEC" || type == "ENDTAB" || type == "EOF";
    return groups.size() == 2 && type == "SECTION" && groups[1].code == 2;
}

void DxfReader::capture(const GroupCode& gc) {
    if (gc.code == 0) {
        if (!finished_record_.empty() && !is_marker(finished_record_)) {
            strays_.push_back(std::move(finished_record_));
        }
        finished_record_ = std::move(open_record_);
        open_record_ = RawRecord();
    }
    open_record_.append(gc.code, gc.value, gc.code_columns);
}

RawRecord DxfReader::take_record() {
    RawRecord record = std::move(finished_record_);
    finished_record_ = RawRecord();
    return record;
}

// Read from the record's bytes up to its first subclass only (every entity is read so)
std::string DxfReader::head_xdictionary(const RawRecord& record) {
    const std::string& data = record.data_;
    size_t pos = 0;
    bool in_xdictionary = false;
    while (pos + 2 <= data.size()) {
        int columns = 0;
        int code = RawRecord::decode_code(static_cast<uint16_t>(static_cast<uint8_t>(data[pos]) |
                                                                (static_cast<uint8_t>(data[pos + 1]) << 8)), columns);
        pos += 2;
        size_t end = data.find('\0', pos);
        if (end == std::string::npos) end = data.size();
        if (code == 100) break;
        if (code == 102) {
            in_xdictionary = data.compare(pos, end - pos, "{ACAD_XDICTIONARY") == 0;
        } else if (code == 360 && in_xdictionary) {
            return data.substr(pos, end - pos);
        }
        pos = end + 1;
    }
    return {};
}

// Read as head_xdictionary()
std::vector<std::string> DxfReader::head_reactors(const RawRecord& record) {
    const std::string& data = record.data_;
    std::vector<std::string> out;
    size_t pos = 0;
    bool in_reactors = false;
    while (pos + 2 <= data.size()) {
        int columns = 0;
        int code = RawRecord::decode_code(static_cast<uint16_t>(static_cast<uint8_t>(data[pos]) |
                                                                (static_cast<uint8_t>(data[pos + 1]) << 8)), columns);
        pos += 2;
        size_t end = data.find('\0', pos);
        if (end == std::string::npos) end = data.size();
        if (code == 100) break;
        if (code == 102) {
            in_reactors = data.compare(pos, end - pos, "{ACAD_REACTORS") == 0;
        } else if (code == 330 && in_reactors) {
            out.push_back(data.substr(pos, end - pos));
        }
        pos = end + 1;
    }
    return out;
}

void DxfReader::stop_capture() {
    capture_ = false;
    open_record_ = RawRecord();
    finished_record_ = RawRecord();
    strays_.clear();
}

void DxfReader::keep_strays(Drawing& dwg, const std::string& section) {
    if (strays_.empty()) return;
    RawSection raw;
    raw.name = section;
    raw.records = std::move(strays_);
    strays_.clear();
    dwg.raw_sections_.push_back(std::move(raw));
}

// A section HDxf does not interpret, kept record by record. The 0/SECTION record is
// still open when this is called; the first code 0 inside the section closes it.
void DxfReader::capture_section(Drawing& dwg, const std::string& name) {
    RawSection raw;
    raw.name = name;
    bool head_taken = false;
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code != 0) continue;
        if (!head_taken) {
            raw.head = take_record();
            head_taken = true;
        } else {
            raw.records.push_back(take_record());
        }
        if (gc.value == "ENDSEC") break;
    }
    // The classes' DXF names, for the proxies' class ids (step 9.S)
    if (name == "CLASSES") {
        for (const RawRecord& r : raw.records) {
            bool is_class = false;
            std::string dxf_name;
            for (const DxfGroup& g : r.groups()) {
                std::string v = g.value;
                trim_trailing_spaces(v);
                size_t b = 0;
                while (b < v.size() && v[b] == ' ') ++b;
                if (g.code == 0) is_class = v.substr(b) == "CLASS";
                if (g.code == 1) dxf_name = v.substr(b);
            }
            if (is_class) class_names_.push_back(dxf_name);
        }
    }
    dwg.raw_sections_.push_back(std::move(raw));
}

// 90 the number of bytes, then 310 with the data (hex in ASCII, a chunk in binary — read
// as hex either way). A 90 that does not match the data: the data is taken (B26).
void DxfReader::parse_thumbnail(Drawing& dwg) {
    long declared = -1;
    std::vector<uint8_t> bytes;
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) {
            dwg.thumbnail_raw_ = take_record();
            if (gc.value != "ENDSEC") push_back(gc);
            break;
        }
        if (gc.code == 90) declared = std::atol(gc.value.c_str());
        if (gc.code == 310) {
            const std::string& hex = gc.value;
            for (size_t i = 0; i + 1 < hex.size(); i += 2) {
                char pair[3] = {hex[i], hex[i + 1], '\0'};
                bytes.push_back(static_cast<uint8_t>(std::strtoul(pair, nullptr, 16)));
            }
        }
    }
    if (declared >= 0 && static_cast<size_t>(declared) != bytes.size()) {
        dwg.warnings_.push_back("THUMBNAILIMAGE: 90 says " + std::to_string(declared) +
                                " bytes, the data has " + std::to_string(bytes.size()));
    }
    dwg.thumbnail_ = std::move(bytes);
}

void DxfReader::push_back(const GroupCode& gc) {
    pending_ = gc;
    has_pending_ = true;
}

// ---------------------------------------------------------------------------
// Top-level read
// ---------------------------------------------------------------------------

bool DxfReader::read(Drawing& dwg) {
    groups_.detect_format();

    // A file without $ACADVER — for example one with only an ENTITIES section,
    // as many simple exporters write — is read as R12, like AutoCAD does.
    version_ = DxfVersion::R12;
    bool saw_section = false;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0 && gc.value == "EOF") break;   // anything after EOF is ignored

        if (gc.code == 0 && gc.value == "SECTION") {
            GroupCode name;
            if (!read_group(name)) break;
            trim_trailing_spaces(name.value);   // section name is a keyword

            if (name.code == 2) {
                saw_section = true;
                section_ = name.value;
                if (gc.code_columns != 0) markers_["SECTION:" + section_] = gc.code_columns;
                if (name.code_columns != 0) markers_["NAME:" + section_] = name.code_columns;
                // The records are preserved in every version, R12 too (plan F9–F22); only
                // how R13+ records are interpreted depends on the version

                if (name.value == "HEADER") {
                    parse_header(dwg);
                    if (!error_.empty()) return false;   // unsupported $ACADVER
                    RawSection header;
                    header.name = "HEADER";
                    header.head = take_record();
                    dwg.raw_sections_.push_back(std::move(header));
                } else if (name.value == "TABLES") {
                    parse_tables(dwg);
                    keep_strays(dwg, "TABLES");
                } else if (name.value == "BLOCKS") {
                    parse_blocks(dwg);
                    keep_strays(dwg, "BLOCKS");
                } else if (name.value == "ENTITIES") {
                    parse_entities(dwg.entities());
                    keep_strays(dwg, "ENTITIES");
                } else if (name.value == "OBJECTS" && capture_) {
                    parse_objects(dwg);
                } else if (name.value == "THUMBNAILIMAGE" && capture_) {
                    parse_thumbnail(dwg);
                } else if (name.value == "ACDSDATA" && capture_ && version_ >= DxfVersion::R2013) {
                    parse_acdsdata(dwg);
                } else if (capture_) {
                    capture_section(dwg, name.value);
                } else {
                    skip_section();
                }
            }
        }
    }

    if (capture_ && !finished_record_.empty() && !is_marker(finished_record_)) {
        strays_.push_back(take_record());
    }
    if (!markers_.empty()) {
        auto form = std::make_shared<AsciiForm>();
        form->marker_columns = std::move(markers_);
        dwg.source_form_ = std::move(form);
    }
    keep_strays(dwg, "");
    if (capture_ && error_.empty() && r13_plus_) {
        link_block_records(dwg);
        resolve_names(dwg);
        type_acdsdata(dwg);
        resolve_proxy_classes(dwg, warnings_);      // a proxy's class (91) from CLASSES (9.3, proxy_record.cpp)
    }

    if (!error_.empty()) {   // malformed group code
        if (!saw_section) error_ = "Not a DXF file: " + error_;
        return false;
    }
    if (!saw_section) {
        error_ = "Not a DXF file: no SECTION found.";
        return false;
    }
    for (auto& w : warnings_) dwg.warnings_.push_back(std::move(w));
    warnings_.clear();
    return true;
}

// ---------------------------------------------------------------------------
// HEADER section
// ---------------------------------------------------------------------------

void DxfReader::parse_header(Drawing& dwg) {
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0 && gc.value == "ENDSEC") return;

        if (gc.code == 9) {
            std::string var_name = gc.value;

            // Collect all value codes for this variable
            std::vector<std::pair<int, std::string>> values;
            while (read_group(gc)) {
                if (gc.code == 9 || gc.code == 0) {
                    push_back(gc);
                    break;
                }
                values.push_back({gc.code, gc.value});
            }

            // Check for $ACADVER
            if (var_name == "$ACADVER") {
                for (const auto& v : values) {
                    if (v.first == 1) {
                        std::string acadver = v.second;
                        trim_trailing_spaces(acadver);   // a keyword, unlike other strings
                        std::optional<DxfVersion> version = version_from_acadver(acadver);
                        if (!version) {
                            error_ = "Unsupported DXF version: " + acadver +
                                     ". HDXF reads R12 (AC1009), R13 (AC1012), R14 (AC1014)"
                                     " and 2000-2018 (AC1015-AC1032).";
                            return;
                        }
                        version_ = *version;
                        if (version_ != DxfVersion::R12) r13_plus_ = true;
                        if (version_ >= DxfVersion::R2007) code_page_ = utf8_code_page;
                    }
                }
            }

            // Up to 2004, strings are in this code page; from 2007 they are UTF-8
            if (var_name == "$DWGCODEPAGE" && code_page_ != utf8_code_page) {
                for (const auto& v : values) {
                    if (v.first == 3) {
                        code_page_name_ = v.second;
                        trim_trailing_spaces(code_page_name_);
                        code_page_ = code_page_from_name(code_page_name_);
                    }
                }
            }

            // Store the header variable in the drawing using the appropriate setter.
            // For point types (have codes 10, 20, 30), use set_header_point.
            // For others, use the first value's code to determine the type.
            if (!values.empty()) {
                // Check if this is a point variable
                bool has_10 = false;
                bool has_20 = false;
                Point3 pt;
                for (const auto& v : values) {
                    if (v.first == 10) { pt.x = std::atof(v.second.c_str()); has_10 = true; }
                    if (v.first == 20) { pt.y = std::atof(v.second.c_str()); has_20 = true; }
                    if (v.first == 30) { pt.z = std::atof(v.second.c_str()); }
                }

                if (has_10 && has_20) {
                    // It's a point variable — but set_header_point reformats the values.
                    // For round-trip fidelity, store the raw values directly.
                    HeaderVar* existing = dwg.find_header(var_name);
                    if (existing) {
                        existing->values = values;
                    } else {
                        // Use set_header_point to create, then overwrite with raw values
                        dwg.set_header_point(var_name, pt);
                        HeaderVar* h = dwg.find_header(var_name);
                        if (h) h->values = values;
                    }
                } else {
                    // Single-value variable — store raw
                    HeaderVar* existing = dwg.find_header(var_name);
                    if (existing) {
                        existing->values = values;
                    } else {
                        // Create via the appropriate setter, then overwrite with raw values
                        int code = values[0].first;
                        if (code == 1 || code == 2 || code == 3 || code == 6 ||
                            code == 7 || code == 8) {
                            dwg.set_header_string(var_name, code, values[0].second);
                        } else if (code == 70 || code == 62 || code == 71 || code == 72 ||
                                   code == 73 || code == 74 || code == 75 || code == 76 ||
                                   code == 77 || code == 78) {
                            dwg.set_header_int(var_name, code, std::atoi(values[0].second.c_str()));
                        } else if (code == 40 || code == 50) {
                            dwg.set_header_double(var_name, code, std::atof(values[0].second.c_str()));
                        } else {
                            // Generic: store as string
                            dwg.set_header_string(var_name, code, values[0].second);
                        }
                        // Overwrite with raw values for round-trip fidelity
                        HeaderVar* h = dwg.find_header(var_name);
                        if (h) h->values = values;
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// TABLES section
// ---------------------------------------------------------------------------

void DxfReader::parse_tables(Drawing& dwg) {
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0 && gc.value == "ENDSEC") return;

        if (gc.code == 0 && gc.value == "TABLE") {
            GroupCode table_name;
            if (!read_group(table_name)) return;
            trim_trailing_spaces(table_name.value);   // table name is a keyword

            if (table_name.code == 2) {
                // The TABLE record: its own groups up to the first entry or ENDTAB
                while (read_group(gc)) {
                    if (gc.code == 0) { push_back(gc); break; }
                }
                if (capture_) {
                    RawTable raw;
                    raw.name = table_name.value;
                    raw.header = take_record();
                    dwg.raw_tables_.push_back(std::move(raw));
                }

                if (table_name.value == "LAYER") {
                    std::vector<Layer> layers;
                    parse_layer_table(layers);
                    // Every LAYER entry is kept, in file order — add_layer() would merge
                    // equal names and search the list for each entry
                    for (auto& l : layers) dwg.layers_.push_back(std::move(l));
                } else if (table_name.value == "LTYPE") {
                    std::vector<Linetype> linetypes;
                    parse_ltype_table(linetypes);
                    if (capture_) {
                        for (auto& lt : linetypes) dwg.linetypes_.push_back(std::move(lt));
                        linetypes.clear();
                    }
                    for (auto& lt : linetypes) {
                        Linetype& added = dwg.add_linetype(lt.name, lt.description, lt.pattern);
                        added.alignment = lt.alignment;
                        added.total_length = lt.total_length;
                        added.flags = lt.flags;
                        added.handle = lt.handle;
                    }
                } else if (table_name.value == "STYLE") {
                    std::vector<TextStyle> styles;
                    parse_style_table(styles);
                    if (capture_) {
                        for (auto& s : styles) dwg.text_styles_.push_back(std::move(s));
                        styles.clear();
                    }
                    for (auto& s : styles) {
                        TextStyle& added = dwg.add_text_style(s.name, s.font_file);
                        added.fixed_height = s.fixed_height;
                        added.width_factor = s.width_factor;
                        added.oblique_angle = s.oblique_angle;
                        added.generation_flags = s.generation_flags;
                        added.last_height = s.last_height;
                        added.big_font = s.big_font;
                        added.flags = s.flags;
                        added.handle = s.handle;
                    }
                } else if (table_name.value == "VIEW") {
                    std::vector<View> views;
                    parse_view_table(views);
                    for (auto& v : views) {
                        dwg.views().push_back(std::move(v));
                    }
                } else if (table_name.value == "UCS") {
                    std::vector<Ucs> ucs_list;
                    parse_ucs_table(ucs_list);
                    for (auto& u : ucs_list) {
                        dwg.ucs_list().push_back(std::move(u));
                    }
                } else if (table_name.value == "VPORT") {
                    std::vector<Vport> vports;
                    parse_vport_table(vports);
                    for (auto& vp : vports) {
                        dwg.vports().push_back(std::move(vp));
                    }
                } else if (table_name.value == "DIMSTYLE") {
                    std::vector<DimStyle> dimstyles;
                    parse_dimstyle_table(dimstyles);
                    if (capture_) {
                        for (auto& ds : dimstyles) dwg.dim_styles_.push_back(std::move(ds));
                        dimstyles.clear();
                    }
                    for (auto& ds : dimstyles) {
                        DimStyle& added = dwg.add_dim_style(ds.name);
                        added = std::move(ds);
                    }
                } else if (table_name.value == "APPID") {
                    std::vector<AppId> app_ids;
                    parse_appid_table(app_ids);
                    if (capture_) {
                        for (auto& a : app_ids) dwg.app_ids_.push_back(std::move(a));
                        app_ids.clear();
                    }
                    for (auto& a : app_ids) {
                        AppId& added = dwg.add_app_id(a.name);
                        added.flags = a.flags;
                        added.handle = a.handle;
                    }
                } else if (capture_) {
                    capture_table(dwg.raw_tables_.back());
                } else {
                    skip_table();
                }
                // Records inside the table that no table parser took
                if (capture_) {
                    for (auto& r : strays_) dwg.raw_tables_.back().entries.push_back(std::move(r));
                    strays_.clear();
                }
            } else {
                push_back(table_name);
                skip_table();
            }
        }
    }
}

// Entries of a table HDxf does not interpret (BLOCK_RECORD, unknown), up to ENDTAB
void DxfReader::capture_table(RawTable& table) {
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code != 0) continue;
        RawRecord record = take_record();
        if (!record.empty()) table.entries.push_back(std::move(record));
        if (gc.value == "ENDTAB") return;
    }
}

void DxfReader::skip_table() {
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0 && gc.value == "ENDTAB") return;
    }
}

// ---------------------------------------------------------------------------
// Skip unknown section
// ---------------------------------------------------------------------------

void DxfReader::skip_section() {
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0 && gc.value == "ENDSEC") return;
    }
}

// ---------------------------------------------------------------------------
// LAYER table
// ---------------------------------------------------------------------------

void DxfReader::parse_layer_table(std::vector<Layer>& layers) {
    GroupCode gc;

    // Skip table header codes until first entry or ENDTAB
    while (read_group(gc)) {
        if (gc.code == 0) {
            if (gc.value == "ENDTAB") return;

            if (gc.value == "LAYER") {
                Layer layer;

                while (read_group(gc)) {
                    if (gc.code == 0) {
                        // Start of next entry or ENDTAB — process what we have
                        layer.raw = take_record();
                        if (r13_plus_) read_layer_xdata(layer);
                        layers.push_back(std::move(layer));
                        if (gc.value == "ENDTAB") return;
                        if (gc.value == "LAYER") {
                            // Reset for next layer
                            layer = Layer{};
                            continue;
                        }
                        push_back(gc);
                        break;
                    }

                    switch (gc.code) {
                        case 5:
                            layer.handle = gc.value;
                            break;
                        case 2:
                            layer.name = gc.value;
                            break;
                        case 6:
                            layer.linetype = gc.value;
                            break;
                        case 62: {
                            int color = std::atoi(gc.value.c_str());
                            if (color < 0) {
                                layer.on = false;
                                color = -color;
                            }
                            layer.color.aci = color;
                            break;
                        }
                        // 2004+: true color and color book name, after 62
                        case 420:
                            layer.color.rgb = std::atoi(gc.value.c_str());
                            break;
                        case 430:
                            layer.color.name = gc.value;
                            break;
                        case 70: {
                            int flags = std::atoi(gc.value.c_str());
                            layer.flags = flags;
                            layer.frozen = (flags & 1) != 0;
                            layer.frozen_in_new_vp = (flags & 2) != 0;
                            layer.locked = (flags & 4) != 0;
                            break;
                        }
                        case 290: layer.plot = std::atoi(gc.value.c_str()) != 0; break;
                        case 370: layer.lineweight = std::atoi(gc.value.c_str()); break;
                        case 390: layer.plot_style_handle = gc.value; break;
                        case 347: layer.material_handle = gc.value; break;     // 2007+
                        case 348: layer.pointer_348 = gc.value; break;         // 2013+
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// LTYPE table
// ---------------------------------------------------------------------------

void DxfReader::parse_ltype_table(std::vector<Linetype>& linetypes) {
    GroupCode gc;

    while (read_group(gc)) {
        if (gc.code == 0) {
            if (gc.value == "ENDTAB") return;

            if (gc.value == "LTYPE") {
                Linetype lt;
                while (read_group(gc)) {
                    if (gc.code == 0) {
                        lt.raw = take_record();
                        lt.xdictionary = head_xdictionary(lt.raw);
                        // Only simple dashes (R12, or 74 = 0 everywhere): no elements
                        bool simple = true;
                        for (const auto& el : lt.elements) simple = simple && el.type == 0;
                        if (simple) lt.elements.clear();
                        // Skip standard linetypes (BYBLOCK, BYLAYER, CONTINUOUS) —
                        // except in R13+, where other records refer to their handles
                        bool standard = lt.name == "ByBlock" || lt.name == "ByLayer" ||
                                        lt.name == "Continuous" || lt.name == "BYBLOCK" ||
                                        lt.name == "BYLAYER" || lt.name == "CONTINUOUS";
                        if (capture_ || !standard) {
                            linetypes.push_back(std::move(lt));
                        }
                        if (gc.value == "ENDTAB") return;
                        if (gc.value == "LTYPE") {
                            lt = Linetype{};
                            continue;
                        }
                        push_back(gc);
                        break;
                    }

                    switch (gc.code) {
                        case 5:  lt.handle = gc.value; break;
                        case 2:  lt.name = gc.value; break;
                        case 70: lt.flags = std::atoi(gc.value.c_str()); break;
                        case 3:  lt.description = gc.value; break;
                        case 72: lt.alignment = std::atoi(gc.value.c_str()); break;
                        case 73: /* element_count — implicit in pattern vector */ break;
                        case 40: lt.total_length = std::atof(gc.value.c_str()); break;
                        case 49:
                            lt.pattern.push_back(std::atof(gc.value.c_str()));
                            lt.elements.emplace_back();
                            break;
                    }
                    // 2000+: the element of the last dash (its style, 340, is resolved
                    // to a name after reading)
                    if (!lt.elements.empty()) {
                        LinetypeElement& el = lt.elements.back();
                        switch (gc.code) {
                            case 74: el.type = std::atoi(gc.value.c_str()); break;
                            case 75: el.shape_number = std::atoi(gc.value.c_str()); break;
                            case 46: el.scale = std::atof(gc.value.c_str()); break;
                            case 50: el.rotation = std::atof(gc.value.c_str()); break;
                            case 44: el.offset.x = std::atof(gc.value.c_str()); break;
                            case 45: el.offset.y = std::atof(gc.value.c_str()); break;
                            case 9:  el.text = gc.value; break;
                        }
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// STYLE table
// ---------------------------------------------------------------------------

void DxfReader::parse_style_table(std::vector<TextStyle>& styles) {
    GroupCode gc;

    while (read_group(gc)) {
        if (gc.code == 0) {
            if (gc.value == "ENDTAB") return;

            if (gc.value == "STYLE") {
                TextStyle s;

                while (read_group(gc)) {
                    if (gc.code == 0) {
                        s.raw = take_record();
                        if (r13_plus_) read_style_xdata(s);
                        styles.push_back(std::move(s));
                        if (gc.value == "ENDTAB") return;
                        if (gc.value == "STYLE") {
                            s = TextStyle{};
                            continue;
                        }
                        push_back(gc);
                        break;
                    }

                    switch (gc.code) {
                        case 5:  s.handle = gc.value; break;
                        case 2:  s.name = gc.value; break;
                        case 70: s.flags = std::atoi(gc.value.c_str()); break;
                        case 40: s.fixed_height = std::atof(gc.value.c_str()); break;
                        case 41: s.width_factor = std::atof(gc.value.c_str()); break;
                        case 50: s.oblique_angle = std::atof(gc.value.c_str()); break;
                        case 71: s.generation_flags = std::atoi(gc.value.c_str()); break;
                        case 42: s.last_height = std::atof(gc.value.c_str()); break;
                        case 3:  s.font_file = gc.value; break;
                        case 4:  s.big_font = gc.value; break;
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// VIEW table
// ---------------------------------------------------------------------------

void DxfReader::parse_view_table(std::vector<View>& views) {
    GroupCode gc;

    while (read_group(gc)) {
        if (gc.code == 0) {
            if (gc.value == "ENDTAB") return;

            if (gc.value == "VIEW") {
                View v;

                while (read_group(gc)) {
                    if (gc.code == 0) {
                        v.raw = take_record();
                        views.push_back(std::move(v));
                        if (gc.value == "ENDTAB") return;
                        if (gc.value == "VIEW") {
                            v = View{};
                            continue;
                        }
                        push_back(gc);
                        break;
                    }

                    switch (gc.code) {
                        case 5:  v.handle = gc.value; break;
                        case 2:  v.name = gc.value; break;
                        case 70: v.flags = std::atoi(gc.value.c_str()); break;
                        case 40: v.height = std::atof(gc.value.c_str()); break;
                        case 41: v.width = std::atof(gc.value.c_str()); break;
                        case 10: v.center.x = std::atof(gc.value.c_str()); break;
                        case 20: v.center.y = std::atof(gc.value.c_str()); break;
                        case 11: v.direction.x = std::atof(gc.value.c_str()); break;
                        case 21: v.direction.y = std::atof(gc.value.c_str()); break;
                        case 31: v.direction.z = std::atof(gc.value.c_str()); break;
                        case 12: v.target.x = std::atof(gc.value.c_str()); break;
                        case 22: v.target.y = std::atof(gc.value.c_str()); break;
                        case 32: v.target.z = std::atof(gc.value.c_str()); break;
                        case 42: v.lens_length = std::atof(gc.value.c_str()); break;
                        case 43: v.front_clip = std::atof(gc.value.c_str()); break;
                        case 44: v.back_clip = std::atof(gc.value.c_str()); break;
                        case 50: v.twist = std::atof(gc.value.c_str()); break;
                        case 71: v.view_mode = std::atoi(gc.value.c_str()); break;
                        // 2000+
                        case 281: v.render_mode = std::atoi(gc.value.c_str()); break;
                        case 72:  v.has_ucs = std::atoi(gc.value.c_str()) != 0; break;
                        case 110: v.ucs_origin.x = std::atof(gc.value.c_str()); break;
                        case 120: v.ucs_origin.y = std::atof(gc.value.c_str()); break;
                        case 130: v.ucs_origin.z = std::atof(gc.value.c_str()); break;
                        case 111: v.ucs_x_axis.x = std::atof(gc.value.c_str()); break;
                        case 121: v.ucs_x_axis.y = std::atof(gc.value.c_str()); break;
                        case 131: v.ucs_x_axis.z = std::atof(gc.value.c_str()); break;
                        case 112: v.ucs_y_axis.x = std::atof(gc.value.c_str()); break;
                        case 122: v.ucs_y_axis.y = std::atof(gc.value.c_str()); break;
                        case 132: v.ucs_y_axis.z = std::atof(gc.value.c_str()); break;
                        case 79:  v.ucs_ortho_type = std::atoi(gc.value.c_str()); break;
                        case 146: v.elevation = std::atof(gc.value.c_str()); break;
                        // 2007+
                        case 73:  v.camera_plottable = std::atoi(gc.value.c_str()) != 0; break;
                        case 332: v.background_handle = gc.value; break;
                        case 334: v.live_section_handle = gc.value; break;
                        case 348: v.visual_style_handle = gc.value; break;
                        case 361: v.sun_handle = gc.value; break;
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// UCS table
// ---------------------------------------------------------------------------

void DxfReader::parse_ucs_table(std::vector<Ucs>& ucs_list) {
    GroupCode gc;

    while (read_group(gc)) {
        if (gc.code == 0) {
            if (gc.value == "ENDTAB") return;

            if (gc.value == "UCS") {
                Ucs u;

                while (read_group(gc)) {
                    if (gc.code == 0) {
                        u.raw = take_record();
                        ucs_list.push_back(std::move(u));
                        if (gc.value == "ENDTAB") return;
                        if (gc.value == "UCS") {
                            u = Ucs{};
                            continue;
                        }
                        push_back(gc);
                        break;
                    }

                    switch (gc.code) {
                        case 5:  u.handle = gc.value; break;
                        case 2:  u.name = gc.value; break;
                        case 70: u.flags = std::atoi(gc.value.c_str()); break;
                        case 10: u.origin.x = std::atof(gc.value.c_str()); break;
                        case 20: u.origin.y = std::atof(gc.value.c_str()); break;
                        case 30: u.origin.z = std::atof(gc.value.c_str()); break;
                        case 11: u.x_dir.x = std::atof(gc.value.c_str()); break;
                        case 21: u.x_dir.y = std::atof(gc.value.c_str()); break;
                        case 31: u.x_dir.z = std::atof(gc.value.c_str()); break;
                        case 12: u.y_dir.x = std::atof(gc.value.c_str()); break;
                        case 22: u.y_dir.y = std::atof(gc.value.c_str()); break;
                        case 32: u.y_dir.z = std::atof(gc.value.c_str()); break;
                        case 79: u.ortho_type = std::atoi(gc.value.c_str()); break;
                        case 146: u.elevation = std::atof(gc.value.c_str()); break;
                        // An orthographic view: 71, then its origin (346 is resolved to
                        // a name after reading)
                        case 71: u.orthographic.push_back({std::atoi(gc.value.c_str()), {}}); break;
                        case 13: if (!u.orthographic.empty()) u.orthographic.back().origin.x = std::atof(gc.value.c_str()); break;
                        case 23: if (!u.orthographic.empty()) u.orthographic.back().origin.y = std::atof(gc.value.c_str()); break;
                        case 33: if (!u.orthographic.empty()) u.orthographic.back().origin.z = std::atof(gc.value.c_str()); break;
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// VPORT table
// ---------------------------------------------------------------------------

void DxfReader::parse_vport_table(std::vector<Vport>& vports) {
    GroupCode gc;

    while (read_group(gc)) {
        if (gc.code == 0) {
            if (gc.value == "ENDTAB") return;

            if (gc.value == "VPORT") {
                Vport vp;

                while (read_group(gc)) {
                    if (gc.code == 0) {
                        vp.raw = take_record();
                        vports.push_back(std::move(vp));
                        if (gc.value == "ENDTAB") return;
                        if (gc.value == "VPORT") {
                            vp = Vport{};
                            continue;
                        }
                        push_back(gc);
                        break;
                    }

                    switch (gc.code) {
                        case 5:  vp.handle = gc.value; break;
                        case 2:  vp.name = gc.value; break;
                        case 70: vp.flags = std::atoi(gc.value.c_str()); break;
                        case 10: vp.lower_left.x = std::atof(gc.value.c_str()); break;
                        case 20: vp.lower_left.y = std::atof(gc.value.c_str()); break;
                        case 11: vp.upper_right.x = std::atof(gc.value.c_str()); break;
                        case 21: vp.upper_right.y = std::atof(gc.value.c_str()); break;
                        case 12: vp.center.x = std::atof(gc.value.c_str()); break;
                        case 22: vp.center.y = std::atof(gc.value.c_str()); break;
                        case 13: vp.snap_base.x = std::atof(gc.value.c_str()); break;
                        case 23: vp.snap_base.y = std::atof(gc.value.c_str()); break;
                        case 14: vp.snap_spacing.x = std::atof(gc.value.c_str()); break;
                        case 24: vp.snap_spacing.y = std::atof(gc.value.c_str()); break;
                        case 15: vp.grid_spacing.x = std::atof(gc.value.c_str()); break;
                        case 25: vp.grid_spacing.y = std::atof(gc.value.c_str()); break;
                        case 16: vp.view_dir.x = std::atof(gc.value.c_str()); break;
                        case 26: vp.view_dir.y = std::atof(gc.value.c_str()); break;
                        case 36: vp.view_dir.z = std::atof(gc.value.c_str()); break;
                        case 17: vp.target.x = std::atof(gc.value.c_str()); break;
                        case 27: vp.target.y = std::atof(gc.value.c_str()); break;
                        case 37: vp.target.z = std::atof(gc.value.c_str()); break;
                        case 40: vp.view_height = std::atof(gc.value.c_str()); break;
                        case 41: vp.aspect_ratio = std::atof(gc.value.c_str()); break;
                        case 42: vp.lens_length = std::atof(gc.value.c_str()); break;
                        case 43: vp.front_clip = std::atof(gc.value.c_str()); break;
                        case 44: vp.back_clip = std::atof(gc.value.c_str()); break;
                        case 50: vp.snap_angle = std::atof(gc.value.c_str()); break;
                        case 51: vp.twist_angle = std::atof(gc.value.c_str()); break;
                        case 71: vp.view_mode = std::atoi(gc.value.c_str()); break;
                        case 72: vp.circle_zoom = std::atoi(gc.value.c_str()); break;
                        case 73: vp.fast_zoom = std::atoi(gc.value.c_str()); break;
                        case 74: vp.ucsicon = std::atoi(gc.value.c_str()); break;
                        case 75: vp.snap_on = std::atoi(gc.value.c_str()); break;
                        case 76: vp.grid_on = std::atoi(gc.value.c_str()); break;
                        case 77: vp.snap_style = std::atoi(gc.value.c_str()); break;
                        case 78: vp.snap_isopair = std::atoi(gc.value.c_str()); break;
                        case 68: vp.status = std::atoi(gc.value.c_str()); break;
                        case 69: vp.id = std::atoi(gc.value.c_str()); break;
                        // 2000+
                        case 281: vp.render_mode = std::atoi(gc.value.c_str()); break;
                        case 65:  vp.ucs_per_viewport = std::atoi(gc.value.c_str()) != 0; break;
                        case 110: vp.ucs_origin.x = std::atof(gc.value.c_str()); break;
                        case 120: vp.ucs_origin.y = std::atof(gc.value.c_str()); break;
                        case 130: vp.ucs_origin.z = std::atof(gc.value.c_str()); break;
                        case 111: vp.ucs_x_axis.x = std::atof(gc.value.c_str()); break;
                        case 121: vp.ucs_x_axis.y = std::atof(gc.value.c_str()); break;
                        case 131: vp.ucs_x_axis.z = std::atof(gc.value.c_str()); break;
                        case 112: vp.ucs_y_axis.x = std::atof(gc.value.c_str()); break;
                        case 122: vp.ucs_y_axis.y = std::atof(gc.value.c_str()); break;
                        case 132: vp.ucs_y_axis.z = std::atof(gc.value.c_str()); break;
                        case 79:  vp.ucs_ortho_type = std::atoi(gc.value.c_str()); break;
                        case 146: vp.elevation = std::atof(gc.value.c_str()); break;
                        // 2007+
                        case 348: vp.visual_style_handle = gc.value; break;
                        case 60:  vp.grid_flags = std::atoi(gc.value.c_str()); break;
                        case 61:  vp.grid_major = std::atoi(gc.value.c_str()); break;
                        case 292: vp.default_lighting = std::atoi(gc.value.c_str()) != 0; break;
                        case 282: vp.default_lighting_type = std::atoi(gc.value.c_str()); break;
                        case 141: vp.brightness = std::atof(gc.value.c_str()); break;
                        case 142: vp.contrast = std::atof(gc.value.c_str()); break;
                        case 63:  vp.ambient_color = Color::from_aci(std::atoi(gc.value.c_str())); break;
                        case 421: vp.ambient_color.rgb = std::atoi(gc.value.c_str()); break;
                        case 431: vp.ambient_color.name = gc.value; break;
                        case 332: vp.background_handle = gc.value; break;
                        case 333: vp.shade_plot_handle = gc.value; break;
                        case 361: vp.sun_handle = gc.value; break;
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// DIMSTYLE table
// ---------------------------------------------------------------------------

void DxfReader::parse_dimstyle_table(std::vector<DimStyle>& dimstyles) {
    GroupCode gc;

    while (read_group(gc)) {
        if (gc.code == 0) {
            if (gc.value == "ENDTAB") return;

            if (gc.value == "DIMSTYLE") {
                // R13+ writes only what differs from AutoCAD's defaults; R12 writes all
                DimStyle ds = r13_plus_ ? autocad_dim_style() : DimStyle{};
                bool flags_read = false;     // 2007+: a second 70 is DIMTFILLCLR

                while (read_group(gc)) {
                    if (gc.code == 0) {
                        ds.raw = take_record();
                        dimstyles.push_back(std::move(ds));
                        if (gc.value == "ENDTAB") return;
                        if (gc.value == "DIMSTYLE") {
                            ds = r13_plus_ ? autocad_dim_style() : DimStyle{};
                            flags_read = false;
                            continue;
                        }
                        push_back(gc);
                        break;
                    }
                    if (gc.code >= 1000) {
                        // 2000+: read from the record after reading (object_reader); R12 here
                        if (r13_plus_) continue;
                        if (gc.code == 1001) ds.xdata.push_back({gc.value, {}});
                        else if (!ds.xdata.empty()) ds.xdata.back().items.push_back({gc.code, gc.value});
                        continue;
                    }

                    switch (gc.code) {
                        case 2:   ds.name = gc.value; break;
                        case 70:
                            if (!flags_read) ds.flags = std::atoi(gc.value.c_str());
                            else ds.dimtfillclr = Color::from_aci(std::atoi(gc.value.c_str()));
                            flags_read = true;
                            break;
                        // 2007+ (the linetypes 345-347 are resolved to names after reading)
                        case 69:  ds.dimtfill = std::atoi(gc.value.c_str()); break;
                        case 290: ds.dimfxlon = std::atoi(gc.value.c_str()); break;
                        case 295: ds.dimtxtdirection = std::atoi(gc.value.c_str()); break;     // 2010+
                        case 49:  ds.dimfxl = std::atof(gc.value.c_str()); break;
                        case 50:  ds.dimjogang = std::atof(gc.value.c_str()); break;
                        // String variables
                        case 3:   ds.dimpost = gc.value; break;
                        case 4:   ds.dimapost = gc.value; break;
                        case 5:   ds.dimblk = gc.value; break;   // R12 … R14: DIMBLK name, not a handle
                        case 105: ds.handle = gc.value; break;   // R13+: the handle
                        case 6:   ds.dimblk1 = gc.value; break;
                        case 7:   ds.dimblk2 = gc.value; break;
                        // Double variables (group 40-48)
                        case 40:  ds.dimscale = std::atof(gc.value.c_str()); break;
                        case 41:  ds.dimasz = std::atof(gc.value.c_str()); break;
                        case 42:  ds.dimexo = std::atof(gc.value.c_str()); break;
                        case 43:  ds.dimdli = std::atof(gc.value.c_str()); break;
                        case 44:  ds.dimexe = std::atof(gc.value.c_str()); break;
                        case 45:  ds.dimrnd = std::atof(gc.value.c_str()); break;
                        case 46:  ds.dimdle = std::atof(gc.value.c_str()); break;
                        case 47:  ds.dimtp = std::atof(gc.value.c_str()); break;
                        case 48:  ds.dimtm = std::atof(gc.value.c_str()); break;
                        // Double variables (group 140-147)
                        case 140: ds.dimtxt = std::atof(gc.value.c_str()); break;
                        case 141: ds.dimcen = std::atof(gc.value.c_str()); break;
                        case 142: ds.dimtsz = std::atof(gc.value.c_str()); break;
                        case 143: ds.dimaltf = std::atof(gc.value.c_str()); break;
                        case 144: ds.dimlfac = std::atof(gc.value.c_str()); break;
                        case 145: ds.dimtvp = std::atof(gc.value.c_str()); break;
                        case 146: ds.dimtfac = std::atof(gc.value.c_str()); break;
                        case 147: ds.dimgap = std::atof(gc.value.c_str()); break;
                        // Int variables (group 71-78)
                        case 71:  ds.dimtol = std::atoi(gc.value.c_str()); break;
                        case 72:  ds.dimlim = std::atoi(gc.value.c_str()); break;
                        case 73:  ds.dimtih = std::atoi(gc.value.c_str()); break;
                        case 74:  ds.dimtoh = std::atoi(gc.value.c_str()); break;
                        case 75:  ds.dimse1 = std::atoi(gc.value.c_str()); break;
                        case 76:  ds.dimse2 = std::atoi(gc.value.c_str()); break;
                        case 77:  ds.dimtad = std::atoi(gc.value.c_str()); break;
                        case 78:  ds.dimzin = std::atoi(gc.value.c_str()); break;
                        // Int variables (group 170-178)
                        case 170: ds.dimalt = std::atoi(gc.value.c_str()); break;
                        case 171: ds.dimaltd = std::atoi(gc.value.c_str()); break;
                        case 172: ds.dimtofl = std::atoi(gc.value.c_str()); break;
                        case 173: ds.dimsah = std::atoi(gc.value.c_str()); break;
                        case 174: ds.dimtix = std::atoi(gc.value.c_str()); break;
                        case 175: ds.dimsoxd = std::atoi(gc.value.c_str()); break;
                        case 176: ds.dimclrd = std::atoi(gc.value.c_str()); break;
                        case 177: ds.dimclre = std::atoi(gc.value.c_str()); break;
                        case 178: ds.dimclrt = std::atoi(gc.value.c_str()); break;
                        // 2000+ (the handles 340-344 are resolved to names after reading)
                        case 148: ds.dimaltrnd = std::atof(gc.value.c_str()); break;
                        case 79:  ds.dimazin = std::atoi(gc.value.c_str()); break;
                        case 90:  ds.dimarcsym = std::atoi(gc.value.c_str()); break;   // 2007+
                        case 179: ds.dimadec = std::atoi(gc.value.c_str()); break;
                        case 271: ds.dimdec = std::atoi(gc.value.c_str()); break;
                        case 272: ds.dimtdec = std::atoi(gc.value.c_str()); break;
                        case 273: ds.dimaltu = std::atoi(gc.value.c_str()); break;
                        case 274: ds.dimalttd = std::atoi(gc.value.c_str()); break;
                        case 275: ds.dimaunit = std::atoi(gc.value.c_str()); break;
                        case 276: ds.dimfrac = std::atoi(gc.value.c_str()); break;
                        case 277: ds.dimlunit = std::atoi(gc.value.c_str()); break;
                        case 278: ds.dimdsep = std::atoi(gc.value.c_str()); break;
                        case 279: ds.dimtmove = std::atoi(gc.value.c_str()); break;
                        case 280: ds.dimjust = std::atoi(gc.value.c_str()); break;
                        case 281: ds.dimsd1 = std::atoi(gc.value.c_str()); break;
                        case 282: ds.dimsd2 = std::atoi(gc.value.c_str()); break;
                        case 283: ds.dimtolj = std::atoi(gc.value.c_str()); break;
                        case 284: ds.dimtzin = std::atoi(gc.value.c_str()); break;
                        case 285: ds.dimaltz = std::atoi(gc.value.c_str()); break;
                        case 286: ds.dimalttz = std::atoi(gc.value.c_str()); break;
                        case 288: ds.dimupt = std::atoi(gc.value.c_str()); break;
                        case 289: ds.dimatfit = std::atoi(gc.value.c_str()); break;
                        // R13 and R14: DIMUNIT and DIMFIT, which 2000 split (B91, dimstyle_legacy.hpp)
                        case 270:
                            if (version_ < DxfVersion::R2000) set_dimunit(ds, std::atoi(gc.value.c_str()));
                            break;
                        case 287:
                            if (version_ < DxfVersion::R2000) set_dimfit(ds, std::atoi(gc.value.c_str()));
                            break;
                        case 371: ds.dimlwd = std::atoi(gc.value.c_str()); break;
                        case 372: ds.dimlwe = std::atoi(gc.value.c_str()); break;
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// APPID table
// ---------------------------------------------------------------------------

void DxfReader::parse_appid_table(std::vector<AppId>& app_ids) {
    GroupCode gc;

    while (read_group(gc)) {
        if (gc.code == 0) {
            if (gc.value == "ENDTAB") return;

            if (gc.value == "APPID") {
                AppId a;

                while (read_group(gc)) {
                    if (gc.code == 0) {
                        a.raw = take_record();
                        // Skip ACAD (it's always present by default) — except in R13+,
                        // where other records refer to its handle
                        if (capture_ || a.name != "ACAD") {
                            app_ids.push_back(std::move(a));
                        }
                        if (gc.value == "ENDTAB") return;
                        if (gc.value == "APPID") {
                            a = AppId{};
                            continue;
                        }
                        push_back(gc);
                        break;
                    }

                    switch (gc.code) {
                        case 5:  a.handle = gc.value; break;
                        case 2:  a.name = gc.value; break;
                        case 70: a.flags = std::atoi(gc.value.c_str()); break;
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// BLOCKS section
// ---------------------------------------------------------------------------

void DxfReader::parse_blocks(Drawing& dwg) {
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0 && gc.value == "ENDSEC") return;

        if (gc.code == 0 && gc.value == "BLOCK") {
            // Read block header codes
            std::string block_name;
            std::string block_layer = "0";
            std::string block_handle;
            int block_paper_space = 0;
            int block_flags = 0;
            int block_unloaded = 0;
            Point3 base_point;
            std::string xref_path;
            Block props;                        // its description and entity properties
            Color block_color, endblk_color;    // 62, 420, 430 of BLOCK and of ENDBLK, each its own
            bool has_block_color = false, has_endblk_color = false;

            while (read_group(gc)) {
                if (gc.code == 0) {
                    push_back(gc);
                    break;
                }
                switch (gc.code) {
                    case 5:  block_handle = gc.value; break;
                    case 2:  block_name = gc.value; break;
                    case 3:  /* also block name, same as 2 */ break;
                    case 8:  block_layer = gc.value; break;
                    case 67: block_paper_space = std::atoi(gc.value.c_str()); break;
                    case 70: block_flags = std::atoi(gc.value.c_str()); break;
                    case 71: block_unloaded = std::atoi(gc.value.c_str()); break;
                    case 10: base_point.x = std::atof(gc.value.c_str()); break;
                    case 20: base_point.y = std::atof(gc.value.c_str()); break;
                    case 30: base_point.z = std::atof(gc.value.c_str()); break;
                    case 1:  xref_path = gc.value; break;
                    // 2000+
                    case 4:   props.description = gc.value; break;
                    case 62:  block_color.aci = std::atoi(gc.value.c_str()); has_block_color = true; break;
                    case 420: block_color.rgb = std::atoi(gc.value.c_str()); has_block_color = true; break;
                    case 430: block_color.name = gc.value; has_block_color = true; break;
                    case 6:   props.linetype = gc.value; break;
                    case 370: props.lineweight = std::atoi(gc.value.c_str()); break;
                    case 48:  props.linetype_scale = std::atof(gc.value.c_str()); break;
                    case 60:  props.visible = std::atoi(gc.value.c_str()) == 0; break;
                }
            }

            RawRecord block_raw = take_record();
            if (capture_) read_entry_head(block_raw, nullptr, nullptr, &props.xdata);

            // Read entities within block until ENDBLK
            std::vector<Entity> block_entities;
            std::string endblk_handle;
            RawRecord endblk_raw;
            while (read_group(gc)) {
                if (gc.code == 0) {
                    if (gc.value == "ENDBLK") {
                        // Read ENDBLK codes (including handle)
                        while (read_group(gc)) {
                            if (gc.code == 5) endblk_handle = gc.value;
                            if (gc.code == 67 && block_paper_space == 0) block_paper_space = std::atoi(gc.value.c_str());
                            if (gc.code == 62) { endblk_color.aci = std::atoi(gc.value.c_str()); has_endblk_color = true; }
                            if (gc.code == 420) { endblk_color.rgb = std::atoi(gc.value.c_str()); has_endblk_color = true; }
                            if (gc.code == 430) { endblk_color.name = gc.value; has_endblk_color = true; }
                            if (gc.code == 0) { push_back(gc); break; }
                        }
                        endblk_raw = take_record();
                        break;
                    }

                    Entity entity;
                    if (parse_entity_record(gc, entity)) {
                        block_entities.push_back(std::move(entity));
                    }
                }
            }

            // Every BLOCK is kept, even with a name that is already there; add_block()
            // would merge them and search the list for each block. Drawing builds its
            // name index once after reading.
            Block& blk = dwg.blocks_.emplace_back();
            blk.name = block_name;
            blk.base_point = base_point;
            blk.raw = std::move(block_raw);
            blk.endblk_raw = std::move(endblk_raw);
            blk.flags = block_flags;
            blk.layer = block_layer;
            blk.paper_space = block_paper_space;
            blk.xref_path = xref_path;
            blk.xref_unloaded = block_unloaded;
            blk.description = std::move(props.description);
            if (has_block_color) blk.block_color = block_color;
            if (has_endblk_color) blk.endblk_color = endblk_color;
            blk.linetype = std::move(props.linetype);
            blk.lineweight = props.lineweight;
            blk.linetype_scale = props.linetype_scale;
            blk.visible = props.visible;
            blk.xdata = std::move(props.xdata);
            blk.handle = block_handle;
            blk.endblk_handle = endblk_handle;
            blk.entities = std::move(block_entities);
        }
    }
}

// ---------------------------------------------------------------------------
// Common entity code parsing
// ---------------------------------------------------------------------------

void DxfReader::parse_common_entity_codes(Entity& entity, const GroupCode& gc) {
    switch (gc.code) {
        case 8:
            entity.props.layer = gc.value;
            break;
        case 6:
            entity.props.linetype = gc.value;
            break;
        case 62:
            entity.props.color.aci = std::atoi(gc.value.c_str());
            break;
        // 2004+, in AcDbEntity after 62: true color and color book name
        case 420:
            entity.props.color.rgb = std::atoi(gc.value.c_str());
            break;
        case 430:
            entity.props.color.name = gc.value;
            break;
        case 440:
            entity.props.transparency.raw = std::atoi(gc.value.c_str());
            break;
        case 48:
            entity.props.linetype_scale = std::atof(gc.value.c_str());
            break;
        case 60:
            entity.props.visible = (std::atoi(gc.value.c_str()) == 0);
            break;
        case 39:
            entity.props.thickness = std::atof(gc.value.c_str());
            break;
        case 210:
            entity.props.extrusion.x = std::atof(gc.value.c_str());
            break;
        case 220:
            entity.props.extrusion.y = std::atof(gc.value.c_str());
            break;
        case 230:
            entity.props.extrusion.z = std::atof(gc.value.c_str());
            break;
        case 67:
            entity.props.paper_space = std::atoi(gc.value.c_str());
            break;
        case 5:
            entity.props.handle = gc.value;
            break;
        // 2000+, in AcDbEntity
        case 370:
            entity.props.lineweight = std::atoi(gc.value.c_str());
            break;
        case 390:
            entity.props.plot_style_handle = gc.value;
            break;
        case 380:
            if (subclass_ == "AcDbEntity") entity.props.plot_style_type = std::atoi(gc.value.c_str());
            break;
        // 2007+, in AcDbEntity after 6: the material (further in, 347 is something else)
        case 347:
            if (subclass_ == "AcDbEntity") entity.props.material_handle = gc.value;
            break;
        // 2007+, in AcDbEntity: the shadow mode (the references' common entity codes)
        case 284:
            if (subclass_ == "AcDbEntity") entity.props.shadow_mode = std::atoi(gc.value.c_str());
            break;
    }
}

// ---------------------------------------------------------------------------
// XDATA parsing
// ---------------------------------------------------------------------------

void DxfReader::parse_xdata(Entity& entity, const GroupCode& first_gc) {
    // first_gc should be code 1001 (app name) or another xdata code
    if (first_gc.code == 1001) {
        XdataList xlist;
        xlist.app_name = first_gc.value;

        GroupCode gc;
        while (read_group(gc)) {
            if (gc.code == 0) {
                // End of entity — push current xdata list and return
                entity.props.xdata.push_back(std::move(xlist));
                push_back(gc);
                return;
            }
            if (gc.code == 1001) {
                // Start of new xdata list — push current and start new
                entity.props.xdata.push_back(std::move(xlist));
                xlist = XdataList{};
                xlist.app_name = gc.value;
                continue;
            }
            if (gc.code < 1000) {
                // Not xdata anymore — push current list and return
                entity.props.xdata.push_back(std::move(xlist));
                push_back(gc);
                return;
            }
            // It's an xdata item (1000-1071, excluding 1001)
            XdataItem item;
            item.code = gc.code;
            item.value = gc.value;
            xlist.items.push_back(std::move(item));
        }

        // End of stream
        entity.props.xdata.push_back(std::move(xlist));
    } else if (first_gc.code >= 1000) {
        // Xdata item without a preceding 1001 — unusual but handle gracefully
        // Create a default xdata list
        XdataList xlist;
        xlist.app_name = "";
        XdataItem item;
        item.code = first_gc.code;
        item.value = first_gc.value;
        xlist.items.push_back(std::move(item));

        GroupCode gc;
        while (read_group(gc)) {
            if (gc.code == 0) {
                entity.props.xdata.push_back(std::move(xlist));
                push_back(gc);
                return;
            }
            if (gc.code == 1001) {
                entity.props.xdata.push_back(std::move(xlist));
                // Recurse for the new app
                parse_xdata(entity, gc);
                return;
            }
            if (gc.code < 1000) {
                entity.props.xdata.push_back(std::move(xlist));
                push_back(gc);
                return;
            }
            XdataItem xi;
            xi.code = gc.code;
            xi.value = gc.value;
            xlist.items.push_back(std::move(xi));
        }

        entity.props.xdata.push_back(std::move(xlist));
    }
}

// ---------------------------------------------------------------------------
// ENTITIES section
// ---------------------------------------------------------------------------

void DxfReader::parse_entities(std::vector<Entity>& entities) {
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) {
            if (gc.value == "ENDSEC") return;

            Entity entity;
            if (parse_entity_record(gc, entity)) {
                entities.push_back(std::move(entity));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Single entity dispatch
// ---------------------------------------------------------------------------

bool DxfReader::parse_single_entity(const GroupCode& type_gc, Entity& out) {
    if (type_gc.code != 0) return false;

    const std::string& t = type_gc.value;

    // A type a phase 9 family has a codec for (TypedRecordTypes.hpp), before the types typed here. The record
    // is read as an unknown one first — that takes it off the stream whole, 102 groups and embedded objects
    // too, and is what stays if the codec cannot read it (B105: kept as read, with a warning); the codec then
    // reads its preserved record, class data of 1010 … 1071 before the first 1001 included
    if (const EntityCodec* codec = TypedRegistry::instance().find_entity(t); codec && capture_) {
        out = parse_unknown(t);
        out.raw = take_record();
        std::vector<DxfGroup> groups;
        if (decode_groups(out.raw, groups)) {
            TypedReadContext context;
            context.version = version_;
            context.binary = groups_.binary();
            context.warnings = &warnings_;
            context.class_names = &class_names_;
            if (!typed_read_entity(*codec, std::move(groups), context, out)) {
                warnings_.push_back(context.error + " (kept as it was read)");
            }
        }
        return true;
    }

    if (t == "LINE")           { out = parse_line(); return true; }
    if (t == "CIRCLE")         { out = parse_circle(); return true; }
    if (t == "ARC")            { out = parse_arc(); return true; }
    if (t == "POINT")          { out = parse_point(); return true; }
    if (t == "TEXT")           { out = parse_text(); return true; }
    if (t == "POLYLINE")       { out = parse_polyline(); return true; }
    if (t == "TRACE")          { out = parse_trace(); return true; }
    if (t == "SOLID")          { out = parse_solid(); return true; }
    if (t == "3DFACE")         { out = parse_3dface(); return true; }
    if (t == "SHAPE")          { out = parse_shape(); return true; }
    if (t == "INSERT")         { out = parse_insert(); return true; }
    if (t == "ATTDEF")         { out = parse_attdef(); return true; }
    if (t == "ATTRIB")         { out = parse_attrib(); return true; }
    if (t == "DIMENSION")      { out = parse_dimension(); return true; }
    if (t == "VIEWPORT")       { out = parse_viewport(); return true; }
    if (t == "LWPOLYLINE")     { out = parse_lwpolyline(); return true; }
    if (t == "ELLIPSE")        { out = parse_ellipse(); return true; }
    if (t == "XLINE")          { out = parse_xline(false); return true; }
    if (t == "RAY")            { out = parse_xline(true); return true; }
    if (t == "MTEXT")          { out = parse_mtext(); return true; }
    if (t == "TOLERANCE")      { out = parse_tolerance(); return true; }
    if (t == "LEADER")         { out = parse_leader(); return true; }
    if (t == "SPLINE")         { out = parse_spline(); return true; }
    if (t == "HATCH")          { out = parse_hatch(); return true; }
    if (t == "MLINE")          { out = parse_mline(); return true; }
    if (t == "IMAGE")          { out = parse_image(); return true; }
    if (t == "WIPEOUT")        { out = parse_image(); out.type = EntityType::Wipeout; return true; }
    if (t == "3DSOLID")        { out = parse_acis(EntityType::Solid3D); return true; }
    if (t == "REGION")         { out = parse_acis(EntityType::Region); return true; }
    if (t == "BODY")           { out = parse_acis(EntityType::Body); return true; }
    if (t == "MULTILEADER")    { out = parse_mleader(); return true; }
    if (t == "SURFACE")        { out = parse_surface(EntityType::Surface); return true; }
    if (t == "EXTRUDEDSURFACE") { out = parse_surface(EntityType::ExtrudedSurface); return true; }
    if (t == "LOFTEDSURFACE")  { out = parse_surface(EntityType::LoftedSurface); return true; }
    if (t == "REVOLVEDSURFACE") { out = parse_surface(EntityType::RevolvedSurface); return true; }
    if (t == "SWEPTSURFACE")   { out = parse_surface(EntityType::SweptSurface); return true; }
    if (t == "PLANESURFACE")   { out = parse_surface(EntityType::PlaneSurface); return true; }
    // (NURBSURFACE is read by its codec since step 9.S: nurbs_surface.cpp)
    if (t == "HELIX")          { out = parse_helix(); return true; }
    if (t == "MESH")           { out = parse_mesh(); return true; }
    // (COORDINATION_MODEL and its reference name are read by their codec since step 9.6: coordination_model.cpp; LIGHT
    // since step 9.11: sun_study.cpp)

    // Unknown entity
    out = parse_unknown(t);
    return true;
}

bool DxfReader::parse_entity_record(const GroupCode& type_gc, Entity& out) {
    if (!parse_single_entity(type_gc, out)) return false;
    // POLYLINE and INSERT take their own record before their VERTEX/ATTRIB records
    if (capture_ && out.raw.empty()) out.raw = take_record();
    if (capture_) {
        out.props.xdictionary = head_xdictionary(out.raw);
        out.props.reactors = head_reactors(out.raw);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Entity parsers
// ---------------------------------------------------------------------------

Entity DxfReader::parse_line() {
    Entity entity;
    entity.type = EntityType::Line;
    LineData d;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        switch (gc.code) {
            case 10: d.start.x = std::atof(gc.value.c_str()); break;
            case 20: d.start.y = std::atof(gc.value.c_str()); break;
            case 30: d.start.z = std::atof(gc.value.c_str()); break;
            case 11: d.end.x = std::atof(gc.value.c_str()); break;
            case 21: d.end.y = std::atof(gc.value.c_str()); break;
            case 31: d.end.z = std::atof(gc.value.c_str()); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }

    entity.data = d;
    return entity;
}

Entity DxfReader::parse_circle() {
    Entity entity;
    entity.type = EntityType::Circle;
    CircleData d;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        switch (gc.code) {
            case 10: d.center.x = std::atof(gc.value.c_str()); break;
            case 20: d.center.y = std::atof(gc.value.c_str()); break;
            case 30: d.center.z = std::atof(gc.value.c_str()); break;
            case 40: d.radius = std::atof(gc.value.c_str()); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }

    entity.data = d;
    return entity;
}

Entity DxfReader::parse_arc() {
    Entity entity;
    entity.type = EntityType::Arc;
    ArcData d;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        switch (gc.code) {
            case 10: d.center.x = std::atof(gc.value.c_str()); break;
            case 20: d.center.y = std::atof(gc.value.c_str()); break;
            case 30: d.center.z = std::atof(gc.value.c_str()); break;
            case 40: d.radius = std::atof(gc.value.c_str()); break;
            case 50: d.start_angle = std::atof(gc.value.c_str()); break;
            case 51: d.end_angle = std::atof(gc.value.c_str()); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }

    entity.data = d;
    return entity;
}

Entity DxfReader::parse_point() {
    Entity entity;
    entity.type = EntityType::Point;
    PointData d;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        switch (gc.code) {
            case 10: d.location.x = std::atof(gc.value.c_str()); break;
            case 20: d.location.y = std::atof(gc.value.c_str()); break;
            case 30: d.location.z = std::atof(gc.value.c_str()); break;
            case 50: d.x_axis_angle = std::atof(gc.value.c_str()); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }

    entity.data = d;
    return entity;
}

Entity DxfReader::parse_text() {
    Entity entity;
    entity.type = EntityType::Text;
    TextData d;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        switch (gc.code) {
            case 10: d.position.x = std::atof(gc.value.c_str()); break;
            case 20: d.position.y = std::atof(gc.value.c_str()); break;
            case 30: d.position.z = std::atof(gc.value.c_str()); break;
            case 40: d.height = std::atof(gc.value.c_str()); break;
            case 1:  d.content = gc.value; break;
            case 50: d.rotation = std::atof(gc.value.c_str()); break;
            case 41: d.width_factor = std::atof(gc.value.c_str()); break;
            case 51: d.oblique_angle = std::atof(gc.value.c_str()); break;
            case 7:  d.style = gc.value; break;
            case 71: if (text_subclass()) d.generation_flags = std::atoi(gc.value.c_str()); break;
            case 72: if (text_subclass()) d.h_justify = std::atoi(gc.value.c_str()); break;
            case 73: d.v_justify = std::atoi(gc.value.c_str()); break;
            case 11: d.alignment_point.x = std::atof(gc.value.c_str()); break;
            case 21: d.alignment_point.y = std::atof(gc.value.c_str()); break;
            case 31: d.alignment_point.z = std::atof(gc.value.c_str()); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }

    entity.data = d;
    return entity;
}

// ---------------------------------------------------------------------------
// POLYLINE + VERTEX + SEQEND
// ---------------------------------------------------------------------------

Entity DxfReader::parse_polyline() {
    Entity entity;
    entity.type = EntityType::Polyline;
    PolylineData d;

    // Read POLYLINE header codes
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        switch (gc.code) {
            case 70: d.flags = std::atoi(gc.value.c_str()); break;
            case 10: d.header_x = std::atof(gc.value.c_str()); break;
            case 20: d.header_y = std::atof(gc.value.c_str()); break;
            case 30: d.elevation = std::atof(gc.value.c_str()); break;
            case 40: d.default_start_width = std::atof(gc.value.c_str()); break;
            case 41: d.default_end_width = std::atof(gc.value.c_str()); break;
            case 71: d.m_count = std::atoi(gc.value.c_str()); break;
            case 72: d.n_count = std::atoi(gc.value.c_str()); break;
            case 73: d.smooth_m = std::atoi(gc.value.c_str()); break;
            case 74: d.smooth_n = std::atoi(gc.value.c_str()); break;
            case 75: d.smooth_type = std::atoi(gc.value.c_str()); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }
    entity.raw = take_record();

    // Read VERTEX entities until SEQEND
    while (read_group(gc)) {
        if (gc.code == 0) {
            if (gc.value == "SEQEND") {
                // SEQEND's handle, and its own layer (when it is not the polyline's), linetype and color
                while (read_group(gc)) {
                    if (gc.code == 5) d.seqend_handle = gc.value;
                    if (gc.code == 8 && gc.value != entity.props.layer) d.seqend_layer = gc.value;
                    if (gc.code == 6) d.seqend_linetype = gc.value;
                    if (gc.code == 62) d.seqend_color.aci = std::atoi(gc.value.c_str());
                    if (gc.code == 0) { push_back(gc); break; }
                }
                d.seqend_raw = take_record();
                break;
            }

            if (gc.value == "VERTEX") {
                Vertex vtx;

                while (read_group(gc)) {
                    if (gc.code == 0) { push_back(gc); break; }
                    switch (gc.code) {
                        case 10: vtx.position.x = std::atof(gc.value.c_str()); break;
                        case 20: vtx.position.y = std::atof(gc.value.c_str()); break;
                        case 30: vtx.position.z = std::atof(gc.value.c_str()); break;
                        case 40: vtx.start_width = std::atof(gc.value.c_str()); break;
                        case 41: vtx.end_width = std::atof(gc.value.c_str()); break;
                        case 42: vtx.bulge = std::atof(gc.value.c_str()); break;
                        case 70: vtx.flags = std::atoi(gc.value.c_str()); break;
                        case 50: vtx.tangent = std::atof(gc.value.c_str()); break;
                        case 71: vtx.face[0] = std::atoi(gc.value.c_str()); break;
                        case 72: vtx.face[1] = std::atoi(gc.value.c_str()); break;
                        case 73: vtx.face[2] = std::atoi(gc.value.c_str()); break;
                        case 74: vtx.face[3] = std::atoi(gc.value.c_str()); break;
                        case 91: vtx.id = std::atoi(gc.value.c_str()); break;
                        case 5:  vtx.handle = gc.value; break;
                        // The vertex's own layer (when it is not the polyline's), linetype, color
                        // and XDATA
                        case 8:  if (gc.value != entity.props.layer) vtx.layer = gc.value; break;
                        case 6:  vtx.linetype = gc.value; break;
                        case 62: vtx.color.aci = std::atoi(gc.value.c_str()); break;
                        default:
                            if (gc.code >= 1000) {
                                Entity holder;
                                parse_xdata(holder, gc);
                                vtx.xdata = std::move(holder.props.xdata);
                            }
                            break;
                    }
                }

                vtx.raw = take_record();
                vtx.reactors = head_reactors(vtx.raw);
                d.vertices.push_back(std::move(vtx));
            } else {
                // Unexpected entity inside POLYLINE — stop reading
                push_back(gc);
                break;
            }
        }
    }

    entity.data = d;
    return entity;
}

// ---------------------------------------------------------------------------
// TRACE
// ---------------------------------------------------------------------------

Entity DxfReader::parse_trace() {
    Entity entity;
    entity.type = EntityType::Trace;
    TraceData d;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        switch (gc.code) {
            case 10: d.p0.x = std::atof(gc.value.c_str()); break;
            case 20: d.p0.y = std::atof(gc.value.c_str()); break;
            case 30: d.p0.z = std::atof(gc.value.c_str()); break;
            case 11: d.p1.x = std::atof(gc.value.c_str()); break;
            case 21: d.p1.y = std::atof(gc.value.c_str()); break;
            case 31: d.p1.z = std::atof(gc.value.c_str()); break;
            case 12: d.p2.x = std::atof(gc.value.c_str()); break;
            case 22: d.p2.y = std::atof(gc.value.c_str()); break;
            case 32: d.p2.z = std::atof(gc.value.c_str()); break;
            case 13: d.p3.x = std::atof(gc.value.c_str()); break;
            case 23: d.p3.y = std::atof(gc.value.c_str()); break;
            case 33: d.p3.z = std::atof(gc.value.c_str()); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }

    entity.data = d;
    return entity;
}

// ---------------------------------------------------------------------------
// SOLID
// ---------------------------------------------------------------------------

Entity DxfReader::parse_solid() {
    Entity entity;
    entity.type = EntityType::Solid;
    SolidData d;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        switch (gc.code) {
            case 10: d.p0.x = std::atof(gc.value.c_str()); break;
            case 20: d.p0.y = std::atof(gc.value.c_str()); break;
            case 30: d.p0.z = std::atof(gc.value.c_str()); break;
            case 11: d.p1.x = std::atof(gc.value.c_str()); break;
            case 21: d.p1.y = std::atof(gc.value.c_str()); break;
            case 31: d.p1.z = std::atof(gc.value.c_str()); break;
            case 12: d.p2.x = std::atof(gc.value.c_str()); break;
            case 22: d.p2.y = std::atof(gc.value.c_str()); break;
            case 32: d.p2.z = std::atof(gc.value.c_str()); break;
            case 13: d.p3.x = std::atof(gc.value.c_str()); break;
            case 23: d.p3.y = std::atof(gc.value.c_str()); break;
            case 33: d.p3.z = std::atof(gc.value.c_str()); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }

    entity.data = d;
    return entity;
}

// ---------------------------------------------------------------------------
// 3DFACE
// ---------------------------------------------------------------------------

Entity DxfReader::parse_3dface() {
    Entity entity;
    entity.type = EntityType::Face3D;
    Face3DData d;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        switch (gc.code) {
            case 10: d.p0.x = std::atof(gc.value.c_str()); break;
            case 20: d.p0.y = std::atof(gc.value.c_str()); break;
            case 30: d.p0.z = std::atof(gc.value.c_str()); break;
            case 11: d.p1.x = std::atof(gc.value.c_str()); break;
            case 21: d.p1.y = std::atof(gc.value.c_str()); break;
            case 31: d.p1.z = std::atof(gc.value.c_str()); break;
            case 12: d.p2.x = std::atof(gc.value.c_str()); break;
            case 22: d.p2.y = std::atof(gc.value.c_str()); break;
            case 32: d.p2.z = std::atof(gc.value.c_str()); break;
            case 13: d.p3.x = std::atof(gc.value.c_str()); break;
            case 23: d.p3.y = std::atof(gc.value.c_str()); break;
            case 33: d.p3.z = std::atof(gc.value.c_str()); break;
            case 70: d.invisible_edges = std::atoi(gc.value.c_str()); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }

    entity.data = d;
    return entity;
}

// ---------------------------------------------------------------------------
// SHAPE
// ---------------------------------------------------------------------------

Entity DxfReader::parse_shape() {
    Entity entity;
    entity.type = EntityType::Shape;
    ShapeData d;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        switch (gc.code) {
            case 10: d.insertion_point.x = std::atof(gc.value.c_str()); break;
            case 20: d.insertion_point.y = std::atof(gc.value.c_str()); break;
            case 30: d.insertion_point.z = std::atof(gc.value.c_str()); break;
            case 40: d.size = std::atof(gc.value.c_str()); break;
            case 2:  d.name = gc.value; break;
            case 50: d.rotation = std::atof(gc.value.c_str()); break;
            case 41: d.x_scale = std::atof(gc.value.c_str()); break;
            case 51: d.oblique_angle = std::atof(gc.value.c_str()); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }

    entity.data = d;
    return entity;
}

// ---------------------------------------------------------------------------
// INSERT (block reference)
// ---------------------------------------------------------------------------

Entity DxfReader::parse_insert() {
    Entity entity;
    entity.type = EntityType::Insert;
    InsertData d;
    int attribs_follow = 0;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        switch (gc.code) {
            case 2:  d.block_name = gc.value; break;
            case 10: d.insertion_point.x = std::atof(gc.value.c_str()); break;
            case 20: d.insertion_point.y = std::atof(gc.value.c_str()); break;
            case 30: d.insertion_point.z = std::atof(gc.value.c_str()); break;
            case 41: d.x_scale = std::atof(gc.value.c_str()); break;
            case 42: d.y_scale = std::atof(gc.value.c_str()); break;
            case 43: d.z_scale = std::atof(gc.value.c_str()); break;
            case 50: d.rotation = std::atof(gc.value.c_str()); break;
            case 70: d.column_count = std::atoi(gc.value.c_str()); break;
            case 71: d.row_count = std::atoi(gc.value.c_str()); break;
            case 44: d.column_spacing = std::atof(gc.value.c_str()); break;
            case 45: d.row_spacing = std::atof(gc.value.c_str()); break;
            case 66: attribs_follow = std::atoi(gc.value.c_str()); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }
    entity.raw = take_record();

    // If attributes follow (code 66 == 1), read ATTRIB entities until SEQEND
    if (attribs_follow == 1) {
        while (read_group(gc)) {
            if (gc.code == 0) {
                if (gc.value == "SEQEND") {
                    // Skip SEQEND codes
                    while (read_group(gc)) {
                        if (gc.code == 5) d.seqend_handle = gc.value;
                        if (gc.code == 0) { push_back(gc); break; }
                    }
                    d.seqend_raw = take_record();
                    break;
                }

                if (gc.value == "ATTRIB") {
                    InsertAttribute ia;
                    bool tag_read = false;
                    MultilineParseState multiline;
                    want_embedded_ = true;
                    // Parse ATTRIB codes
                    while (read_group(gc)) {
                        if (gc.code == 0) { push_back(gc); break; }
                        if (multiline_group(ia.data.multiline, multiline, gc)) continue;
                        switch (gc.code) {
                            case 10: ia.data.start_point.x = std::atof(gc.value.c_str()); break;
                            case 20: ia.data.start_point.y = std::atof(gc.value.c_str()); break;
                            case 30: ia.data.start_point.z = std::atof(gc.value.c_str()); break;
                            case 40: ia.data.height = std::atof(gc.value.c_str()); break;
                            case 1:  ia.data.value = gc.value; break;
                            case 2:  ia.data.tag = gc.value; tag_read = true; break;
                            // The first 280 is the version (2010+), the one after the tag the lock
                            case 280:
                                if (tag_read) ia.data.lock_position = std::atoi(gc.value.c_str()) != 0;
                                else ia.data.version = std::atoi(gc.value.c_str());
                                break;
                            case 70: ia.data.flags = std::atoi(gc.value.c_str()); break;
                            case 73: ia.data.field_length = std::atoi(gc.value.c_str()); break;
                            case 50: ia.data.rotation = std::atof(gc.value.c_str()); break;
                            case 41: ia.data.width_factor = std::atof(gc.value.c_str()); break;
                            case 51: ia.data.oblique_angle = std::atof(gc.value.c_str()); break;
                            case 7:  ia.data.style = gc.value; break;
                            case 71: if (text_subclass()) ia.data.generation_flags = std::atoi(gc.value.c_str()); break;
                            case 72: if (text_subclass()) ia.data.h_justify = std::atoi(gc.value.c_str()); break;
                            case 74: ia.data.v_justify = std::atoi(gc.value.c_str()); break;
                            case 11: ia.data.alignment_point.x = std::atof(gc.value.c_str()); break;
                            case 21: ia.data.alignment_point.y = std::atof(gc.value.c_str()); break;
                            case 31: ia.data.alignment_point.z = std::atof(gc.value.c_str()); break;
                            // Common entity props for the attribute itself
                            case 8:  ia.props.layer = gc.value; break;
                            case 6:  ia.props.linetype = gc.value; break;
                            case 62: ia.props.color.aci = std::atoi(gc.value.c_str()); break;
                            case 420: ia.props.color.rgb = std::atoi(gc.value.c_str()); break;
                            case 430: ia.props.color.name = gc.value; break;
                            case 440: ia.props.transparency.raw = std::atoi(gc.value.c_str()); break;
                            case 48: ia.props.linetype_scale = std::atof(gc.value.c_str()); break;
                            case 60: ia.props.visible = (std::atoi(gc.value.c_str()) == 0); break;
                            case 39: ia.props.thickness = std::atof(gc.value.c_str()); break;
                            case 210: ia.props.extrusion.x = std::atof(gc.value.c_str()); break;
                            case 220: ia.props.extrusion.y = std::atof(gc.value.c_str()); break;
                            case 230: ia.props.extrusion.z = std::atof(gc.value.c_str()); break;
                            case 67: ia.props.paper_space = std::atoi(gc.value.c_str()); break;
                            case 5:  ia.props.handle = gc.value; break;
                            case 370: ia.props.lineweight = std::atoi(gc.value.c_str()); break;
                            case 390: ia.props.plot_style_handle = gc.value; break;
                            case 347:
                                if (subclass_ == "AcDbEntity") ia.props.material_handle = gc.value;
                                break;
                            default:
                                if (gc.code >= 1000) {
                                    Entity holder;
                                    parse_xdata(holder, gc);
                                    ia.props.xdata = std::move(holder.props.xdata);
                                }
                                break;
                        }
                    }
                    want_embedded_ = false;
                    finish_multiline(ia.data.multiline, multiline, ia.props.extrusion);
                    take_attribute_lock(ia.props.xdata, ia.data.lock_position);

                    ia.raw = take_record();
                    ia.props.xdictionary = head_xdictionary(ia.raw);
                    ia.props.reactors = head_reactors(ia.raw);
                    d.attributes.push_back(std::move(ia));
                } else {
                    // Unexpected entity — stop reading attributes
                    push_back(gc);
                    break;
                }
            }
        }
    }

    entity.data = d;
    return entity;
}

// ---------------------------------------------------------------------------
// ATTDEF (attribute definition)
// ---------------------------------------------------------------------------

Entity DxfReader::parse_attdef() {
    Entity entity;
    entity.type = EntityType::Attdef;
    AttdefData d;
    bool tag_read = false;
    MultilineParseState multiline;

    want_embedded_ = true;
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        if (multiline_group(d.multiline, multiline, gc)) continue;
        switch (gc.code) {
            case 10: d.start_point.x = std::atof(gc.value.c_str()); break;
            case 20: d.start_point.y = std::atof(gc.value.c_str()); break;
            case 30: d.start_point.z = std::atof(gc.value.c_str()); break;
            case 40: d.height = std::atof(gc.value.c_str()); break;
            case 1:  d.default_value = gc.value; break;
            case 3:  d.prompt = gc.value; break;
            case 2:  d.tag = gc.value; tag_read = true; break;
            // The first 280 is the version (2010+), the one after the tag the lock
            case 280:
                if (tag_read) d.lock_position = std::atoi(gc.value.c_str()) != 0;
                else d.version = std::atoi(gc.value.c_str());
                break;
            case 70: d.flags = std::atoi(gc.value.c_str()); break;
            case 73: d.field_length = std::atoi(gc.value.c_str()); break;
            case 50: d.rotation = std::atof(gc.value.c_str()); break;
            case 41: d.width_factor = std::atof(gc.value.c_str()); break;
            case 51: d.oblique_angle = std::atof(gc.value.c_str()); break;
            case 7:  d.style = gc.value; break;
            case 71: if (text_subclass()) d.generation_flags = std::atoi(gc.value.c_str()); break;
            case 72: if (text_subclass()) d.h_justify = std::atoi(gc.value.c_str()); break;
            case 74: d.v_justify = std::atoi(gc.value.c_str()); break;
            case 11: d.alignment_point.x = std::atof(gc.value.c_str()); break;
            case 21: d.alignment_point.y = std::atof(gc.value.c_str()); break;
            case 31: d.alignment_point.z = std::atof(gc.value.c_str()); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }
    want_embedded_ = false;
    finish_multiline(d.multiline, multiline, entity.props.extrusion);

    take_attribute_lock(entity.props.xdata, d.lock_position);
    entity.data = d;
    return entity;
}

// ---------------------------------------------------------------------------
// ATTRIB (attribute value)
// ---------------------------------------------------------------------------

Entity DxfReader::parse_attrib() {
    Entity entity;
    entity.type = EntityType::Attrib;
    AttribData d;
    bool tag_read = false;
    MultilineParseState multiline;

    want_embedded_ = true;
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        if (multiline_group(d.multiline, multiline, gc)) continue;
        switch (gc.code) {
            case 10: d.start_point.x = std::atof(gc.value.c_str()); break;
            case 20: d.start_point.y = std::atof(gc.value.c_str()); break;
            case 30: d.start_point.z = std::atof(gc.value.c_str()); break;
            case 40: d.height = std::atof(gc.value.c_str()); break;
            case 1:  d.value = gc.value; break;
            case 2:  d.tag = gc.value; tag_read = true; break;
            // The first 280 is the version (2010+), the one after the tag the lock
            case 280:
                if (tag_read) d.lock_position = std::atoi(gc.value.c_str()) != 0;
                else d.version = std::atoi(gc.value.c_str());
                break;
            case 70: d.flags = std::atoi(gc.value.c_str()); break;
            case 73: d.field_length = std::atoi(gc.value.c_str()); break;
            case 50: d.rotation = std::atof(gc.value.c_str()); break;
            case 41: d.width_factor = std::atof(gc.value.c_str()); break;
            case 51: d.oblique_angle = std::atof(gc.value.c_str()); break;
            case 7:  d.style = gc.value; break;
            case 71: if (text_subclass()) d.generation_flags = std::atoi(gc.value.c_str()); break;
            case 72: if (text_subclass()) d.h_justify = std::atoi(gc.value.c_str()); break;
            case 74: d.v_justify = std::atoi(gc.value.c_str()); break;
            case 11: d.alignment_point.x = std::atof(gc.value.c_str()); break;
            case 21: d.alignment_point.y = std::atof(gc.value.c_str()); break;
            case 31: d.alignment_point.z = std::atof(gc.value.c_str()); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }
    want_embedded_ = false;
    finish_multiline(d.multiline, multiline, entity.props.extrusion);

    take_attribute_lock(entity.props.xdata, d.lock_position);
    entity.data = d;
    return entity;
}

// ---------------------------------------------------------------------------
// DIMENSION
// ---------------------------------------------------------------------------

Entity DxfReader::parse_dimension() {
    Entity entity;
    entity.type = EntityType::Dimension;
    DimensionData d;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        switch (gc.code) {
            case 2:  d.block_name = gc.value; break;
            case 3:  d.dim_style = gc.value; break;
            case 10: d.definition_point.x = std::atof(gc.value.c_str()); break;
            case 20: d.definition_point.y = std::atof(gc.value.c_str()); break;
            case 30: d.definition_point.z = std::atof(gc.value.c_str()); break;
            case 11: d.text_midpoint.x = std::atof(gc.value.c_str()); break;
            case 21: d.text_midpoint.y = std::atof(gc.value.c_str()); break;
            case 31: d.text_midpoint.z = std::atof(gc.value.c_str()); break;
            case 12: d.block_translation.x = std::atof(gc.value.c_str()); break;
            case 22: d.block_translation.y = std::atof(gc.value.c_str()); break;
            case 32: d.block_translation.z = std::atof(gc.value.c_str()); break;
            case 70: {
                int flags = std::atoi(gc.value.c_str());
                d.flags = flags;
                d.dim_type = static_cast<DimensionType>(flags & 7);
                break;
            }
            case 1:  d.text_override = gc.value; break;
            case 13: d.def_point2.x = std::atof(gc.value.c_str()); break;
            case 23: d.def_point2.y = std::atof(gc.value.c_str()); break;
            case 33: d.def_point2.z = std::atof(gc.value.c_str()); break;
            case 14: d.def_point3.x = std::atof(gc.value.c_str()); break;
            case 24: d.def_point3.y = std::atof(gc.value.c_str()); break;
            case 34: d.def_point3.z = std::atof(gc.value.c_str()); break;
            case 15: d.def_point4.x = std::atof(gc.value.c_str()); break;
            case 25: d.def_point4.y = std::atof(gc.value.c_str()); break;
            case 35: d.def_point4.z = std::atof(gc.value.c_str()); break;
            case 16: d.arc_point.x = std::atof(gc.value.c_str()); break;
            case 26: d.arc_point.y = std::atof(gc.value.c_str()); break;
            case 36: d.arc_point.z = std::atof(gc.value.c_str()); break;
            case 40: d.leader_length = std::atof(gc.value.c_str()); break;
            case 50: d.rotation = std::atof(gc.value.c_str()); break;
            case 51: d.horizontal_dir = std::atof(gc.value.c_str()); break;
            case 52: d.oblique_angle = std::atof(gc.value.c_str()); break;
            case 53: d.text_rotation = std::atof(gc.value.c_str()); break;
            case 280:
                if (subclass_ == "AcDbDimension") d.version = std::atoi(gc.value.c_str());
                else parse_common_entity_codes(entity, gc);
                break;
            // 9.S (Codex 10): AcDbDimension's text groups, kept for the writer (72 and 41, which no AutoCAD file has, are not
            // DIMENSION's: an unchanged record keeps them in its preserved record)
            case 71: case 73: case 74: case 75:
                if (subclass_ != "AcDbDimension") { parse_common_entity_codes(entity, gc); break; }
                if (gc.code == 71) d.attachment_point = static_cast<int16_t>(std::atoi(gc.value.c_str()));
                if (gc.code == 73) d.value_73 = static_cast<int16_t>(std::atoi(gc.value.c_str()));
                if (gc.code == 74) d.value_74 = static_cast<int16_t>(std::atoi(gc.value.c_str()));
                if (gc.code == 75) d.value_75 = static_cast<int16_t>(std::atoi(gc.value.c_str()));
                break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }

    entity.data = d;
    return entity;
}

// ---------------------------------------------------------------------------
// VIEWPORT (paper space)
// ---------------------------------------------------------------------------

Entity DxfReader::parse_viewport() {
    Entity entity;
    entity.type = EntityType::Viewport;
    ViewportData d;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        switch (gc.code) {
            case 10: d.center.x = std::atof(gc.value.c_str()); break;
            case 20: d.center.y = std::atof(gc.value.c_str()); break;
            case 30: d.center.z = std::atof(gc.value.c_str()); break;
            case 40: d.width = std::atof(gc.value.c_str()); break;
            case 41: d.height = std::atof(gc.value.c_str()); break;
            case 68: d.status = std::atoi(gc.value.c_str()); break;
            case 69: d.id = std::atoi(gc.value.c_str()); break;
            // 2000+ (frozen layers, 331, are resolved to names after reading)
            case 12: d.view_center.x = std::atof(gc.value.c_str()); break;
            case 22: d.view_center.y = std::atof(gc.value.c_str()); break;
            case 13: d.snap_base.x = std::atof(gc.value.c_str()); break;
            case 23: d.snap_base.y = std::atof(gc.value.c_str()); break;
            case 14: d.snap_spacing.x = std::atof(gc.value.c_str()); break;
            case 24: d.snap_spacing.y = std::atof(gc.value.c_str()); break;
            case 15: d.grid_spacing.x = std::atof(gc.value.c_str()); break;
            case 25: d.grid_spacing.y = std::atof(gc.value.c_str()); break;
            case 16: d.view_direction.x = std::atof(gc.value.c_str()); break;
            case 26: d.view_direction.y = std::atof(gc.value.c_str()); break;
            case 36: d.view_direction.z = std::atof(gc.value.c_str()); break;
            case 17: d.view_target.x = std::atof(gc.value.c_str()); break;
            case 27: d.view_target.y = std::atof(gc.value.c_str()); break;
            case 37: d.view_target.z = std::atof(gc.value.c_str()); break;
            case 42: d.lens_length = std::atof(gc.value.c_str()); break;
            case 43: d.front_clip = std::atof(gc.value.c_str()); break;
            case 44: d.back_clip = std::atof(gc.value.c_str()); break;
            case 45: d.view_height = std::atof(gc.value.c_str()); break;
            case 50: d.snap_angle = std::atof(gc.value.c_str()); break;
            case 51: d.twist_angle = std::atof(gc.value.c_str()); break;
            case 72: d.circle_zoom = std::atoi(gc.value.c_str()); break;
            case 90: d.status_flags = std::atoi(gc.value.c_str()); break;
            case 1:  d.plot_style_sheet = gc.value; break;
            case 340: d.clip_boundary_handle = gc.value; break;
            case 281: d.render_mode = std::atoi(gc.value.c_str()); break;
            case 71: d.ucs_per_viewport = std::atoi(gc.value.c_str()); break;
            case 74: d.ucs_icon = std::atoi(gc.value.c_str()); break;
            case 110: d.ucs_origin.x = std::atof(gc.value.c_str()); break;
            case 120: d.ucs_origin.y = std::atof(gc.value.c_str()); break;
            case 130: d.ucs_origin.z = std::atof(gc.value.c_str()); break;
            case 111: d.ucs_x_axis.x = std::atof(gc.value.c_str()); break;
            case 121: d.ucs_x_axis.y = std::atof(gc.value.c_str()); break;
            case 131: d.ucs_x_axis.z = std::atof(gc.value.c_str()); break;
            case 112: d.ucs_y_axis.x = std::atof(gc.value.c_str()); break;
            case 122: d.ucs_y_axis.y = std::atof(gc.value.c_str()); break;
            case 132: d.ucs_y_axis.z = std::atof(gc.value.c_str()); break;
            case 79: d.ucs_ortho_type = std::atoi(gc.value.c_str()); break;
            case 146: d.elevation = std::atof(gc.value.c_str()); break;
            case 170: d.shade_plot_mode = std::atoi(gc.value.c_str()); break;
            // 2007+
            case 61:  d.grid_major = std::atoi(gc.value.c_str()); break;
            case 348:
                d.visual_style_handle = gc.value;
                d.visual_style_in_entity = subclass_ == "AcDbEntity";
                break;
            case 292: d.default_lighting = std::atoi(gc.value.c_str()) != 0; break;
            case 282: d.default_lighting_type = std::atoi(gc.value.c_str()); break;
            case 141: d.brightness = std::atof(gc.value.c_str()); break;
            case 142: d.contrast = std::atof(gc.value.c_str()); break;
            case 63:  d.ambient_color = Color::from_aci(std::atoi(gc.value.c_str())); break;
            case 421: d.ambient_color.rgb = std::atoi(gc.value.c_str()); break;
            case 431: d.ambient_color.name = gc.value; break;
            case 332: d.background_handle = gc.value; break;
            case 333: d.shade_plot_handle = gc.value; break;
            case 361: d.sun_handle = gc.value; break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }

    // R13 and R14 keep the view in XDATA ACAD/MVIEW (step 7.16): it becomes the typed fields, so a
    // program edits one place, and is written again from them (DxfWriter::write_viewport_older())
    if (version_ == DxfVersion::R13 || version_ == DxfVersion::R14) {
        auto& list = entity.props.xdata;
        for (auto it = list.begin(); it != list.end(); ++it) {
            if (is_mview(*it) && view_from_mview(*it, d)) {
                list.erase(it);
                break;
            }
        }
    }
    entity.data = d;
    return entity;
}

// ---------------------------------------------------------------------------
// LWPOLYLINE (2000+)
// ---------------------------------------------------------------------------

Entity DxfReader::parse_lwpolyline() {
    Entity entity;
    entity.type = EntityType::LwPolyline;
    LwPolylineData d;
    double constant_width = 0.0;
    bool vertex_widths = false;

    // 10 starts a vertex; 20 and 40–42 belong to the vertex before them. 90 (the count)
    // follows from the vertices; 91 (2010+) is the vertex's id.
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        LwVertex* v = d.vertices.empty() ? nullptr : &d.vertices.back();
        switch (gc.code) {
            case 70: d.flags = std::atoi(gc.value.c_str()); break;
            case 38: d.elevation = std::atof(gc.value.c_str()); break;
            case 43: constant_width = std::atof(gc.value.c_str()); break;
            case 90: break;
            case 91: if (v) v->id = std::atoi(gc.value.c_str()); break;
            case 10:
                d.vertices.emplace_back();
                d.vertices.back().position.x = std::atof(gc.value.c_str());
                break;
            case 20: if (v) v->position.y = std::atof(gc.value.c_str()); break;
            case 40:
                if (v) v->start_width = std::atof(gc.value.c_str());
                vertex_widths = true;
                break;
            case 41:
                if (v) v->end_width = std::atof(gc.value.c_str());
                vertex_widths = true;
                break;
            case 42: if (v) v->bulge = std::atof(gc.value.c_str()); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }
    // 43 is the width of every vertex when they have no widths of their own
    if (!vertex_widths) {
        for (auto& v : d.vertices) v.start_width = v.end_width = constant_width;
    }

    entity.data = std::move(d);
    return entity;
}

// ---------------------------------------------------------------------------
// ELLIPSE (2000+)
// ---------------------------------------------------------------------------

Entity DxfReader::parse_ellipse() {
    Entity entity;
    entity.type = EntityType::Ellipse;
    EllipseData d;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        switch (gc.code) {
            case 10: d.center.x = std::atof(gc.value.c_str()); break;
            case 20: d.center.y = std::atof(gc.value.c_str()); break;
            case 30: d.center.z = std::atof(gc.value.c_str()); break;
            case 11: d.major_axis.x = std::atof(gc.value.c_str()); break;
            case 21: d.major_axis.y = std::atof(gc.value.c_str()); break;
            case 31: d.major_axis.z = std::atof(gc.value.c_str()); break;
            case 40: d.ratio = std::atof(gc.value.c_str()); break;
            case 41: d.start_param = std::atof(gc.value.c_str()); break;
            case 42: d.end_param = std::atof(gc.value.c_str()); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }

    entity.data = d;
    return entity;
}

// ---------------------------------------------------------------------------
// XLINE and RAY (2000+)
// ---------------------------------------------------------------------------

Entity DxfReader::parse_xline(bool ray) {
    Entity entity;
    entity.type = ray ? EntityType::Ray : EntityType::Xline;
    Point3 base;
    Point3 direction = {1.0, 0.0, 0.0};

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        switch (gc.code) {
            case 10: base.x = std::atof(gc.value.c_str()); break;
            case 20: base.y = std::atof(gc.value.c_str()); break;
            case 30: base.z = std::atof(gc.value.c_str()); break;
            case 11: direction.x = std::atof(gc.value.c_str()); break;
            case 21: direction.y = std::atof(gc.value.c_str()); break;
            case 31: direction.z = std::atof(gc.value.c_str()); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }

    if (ray) entity.data = RayData{base, direction};
    else entity.data = XlineData{base, direction};
    return entity;
}

// ---------------------------------------------------------------------------
// MTEXT (2000+)
// ---------------------------------------------------------------------------

// The text's parts (3 …, then 1) are joined before they are decoded, so a \U+XXXX or a
// character split between two parts is read whole (B16). A rotation (50) gives the
// direction in the OCS; if both 50 and 11 are there, the last one counts (DXF reference).
void DxfReader::finish_mtext(MTextData& d, MTextParseState& state, const Point3& extrusion) {
    mtext_finish(d, state, extrusion);
    if (!decode_string(d.content, 1)) d.content.clear();
}

Entity DxfReader::parse_mtext() {
    Entity entity;
    entity.type = EntityType::MText;
    MTextData d;
    MTextParseState state;
    int embedded = 0;

    keep_text_encoded_ = true;
    want_embedded_ = true;
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        if (gc.code == 101) { ++embedded; continue; }
        // The first embedded object is the columns'; others stay in the preserved record
        if (embedded > 0 && gc.code < 1000) {
            if (embedded == 1) mtext_apply_embedded_group(d, state, gc.code, gc.value);
            continue;
        }
        if (mtext_apply_group(d, state, gc.code, gc.value)) continue;
        if (gc.code >= 1000) { parse_xdata(entity, gc); }
        else { parse_common_entity_codes(entity, gc); }
    }
    keep_text_encoded_ = false;
    want_embedded_ = false;

    finish_mtext(d, state, entity.props.extrusion);
    // Up to 2013 the columns, their links, the frame and the defined height are XDATA
    take_mtext_xdata(entity.props.xdata, d);

    entity.data = std::move(d);
    return entity;
}

// A multiline attribute (plan phase 6, B75): in AcDbAttribute 71 (2 attribute, 4 definition), 72 (the
// number of secondary attributes), their 340 and the alignment point 11, then the embedded object
// with the text's AcDbMText groups (and, for its columns, another embedded object)
bool DxfReader::multiline_group(OptionalBoxed<MultilineAttribute>& ml, MultilineParseState& state,
                                const GroupCode& gc) {
    // (made by the first of 71, 72, 340, 11 or 101: a record with only 71 and 72 — ODA writes them in
    // every attribute — has no text and no alignment point of this part)
    auto start = [&]() {
        if (ml) return;
        ml.emplace();
        ml->has_text = false;
        ml->has_alignment_point = false;
    };
    if (gc.code == 101) {
        start();
        ml->has_text = true;
        ++state.embedded;
        keep_text_encoded_ = true;
        return true;
    }
    if (state.embedded > 0 && gc.code < 1000) {
        // A group the text has no field for stays in the preserved record
        if (state.embedded == 1) mtext_apply_group(ml->text, state.text, gc.code, gc.value);
        else if (state.embedded == 2) mtext_apply_embedded_group(ml->text, state.text, gc.code, gc.value);
        return true;
    }
    if ((subclass_ != "AcDbAttribute" && subclass_ != "AcDbAttributeDefinition") || state.embedded > 0) return false;
    if (gc.code != 71 && gc.code != 72 && gc.code != 340 && gc.code != 11 && gc.code != 21 && gc.code != 31) {
        return false;
    }
    start();
    switch (gc.code) {
        case 71: ml->type = std::atoi(gc.value.c_str()); break;
        case 72: break;                                         // the number of 340
        case 340: ml->secondary.push_back(gc.value); break;
        case 11: ml->alignment_point.x = std::atof(gc.value.c_str()); ml->has_alignment_point = true; break;
        case 21: ml->alignment_point.y = std::atof(gc.value.c_str()); break;
        case 31: ml->alignment_point.z = std::atof(gc.value.c_str()); break;
    }
    return true;
}

void DxfReader::finish_multiline(OptionalBoxed<MultilineAttribute>& ml, MultilineParseState& state,
                                 const Point3& extrusion) {
    keep_text_encoded_ = false;
    if (ml) finish_mtext(ml->text, state.text, extrusion);
}

// ---------------------------------------------------------------------------
// TOLERANCE (2000+)
// ---------------------------------------------------------------------------

Entity DxfReader::parse_tolerance() {
    Entity entity;
    entity.type = EntityType::Tolerance;
    ToleranceData d;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        switch (gc.code) {
            case 3:  d.dim_style = gc.value; break;
            case 10: d.insertion_point.x = std::atof(gc.value.c_str()); break;
            case 20: d.insertion_point.y = std::atof(gc.value.c_str()); break;
            case 30: d.insertion_point.z = std::atof(gc.value.c_str()); break;
            case 1:  d.content = gc.value; break;
            case 11: d.x_axis_direction.x = std::atof(gc.value.c_str()); break;
            case 21: d.x_axis_direction.y = std::atof(gc.value.c_str()); break;
            case 31: d.x_axis_direction.z = std::atof(gc.value.c_str()); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }

    entity.data = std::move(d);
    return entity;
}

// ---------------------------------------------------------------------------
// LEADER (2000+)
// ---------------------------------------------------------------------------

Entity DxfReader::parse_leader() {
    Entity entity;
    entity.type = EntityType::Leader;
    LeaderData d;

    // 10 starts a vertex; 76 (the count) follows from them
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        Point3* v = d.vertices.empty() ? nullptr : &d.vertices.back();
        switch (gc.code) {
            case 3:  d.dim_style = gc.value; break;
            case 71: d.arrowhead = std::atoi(gc.value.c_str()) != 0; break;
            case 72: d.path_type = std::atoi(gc.value.c_str()); break;
            case 73: d.annotation_type = std::atoi(gc.value.c_str()); break;
            case 74: d.hookline_direction = std::atoi(gc.value.c_str()); break;
            case 75: d.hookline = std::atoi(gc.value.c_str()) != 0; break;
            case 40: d.text_height = std::atof(gc.value.c_str()); break;
            case 41: d.text_width = std::atof(gc.value.c_str()); break;
            case 76: break;
            case 10:
                d.vertices.emplace_back();
                d.vertices.back().x = std::atof(gc.value.c_str());
                break;
            case 20: if (v) v->y = std::atof(gc.value.c_str()); break;
            case 30: if (v) v->z = std::atof(gc.value.c_str()); break;
            case 77: d.byblock_color = std::atoi(gc.value.c_str()); break;
            case 340: d.annotation_handle = gc.value; break;
            case 211: d.horizontal_direction.x = std::atof(gc.value.c_str()); break;
            case 221: d.horizontal_direction.y = std::atof(gc.value.c_str()); break;
            case 231: d.horizontal_direction.z = std::atof(gc.value.c_str()); break;
            // R13 has one offset, 212: the annotation's (R13:LEADER); R14 and later have 212 for the block's
            // and 213 for the annotation's (plan phase 7, format profile 7.4)
            case 212: (version_ == DxfVersion::R13 ? d.annotation_offset : d.block_offset).x = std::atof(gc.value.c_str()); break;
            case 222: (version_ == DxfVersion::R13 ? d.annotation_offset : d.block_offset).y = std::atof(gc.value.c_str()); break;
            case 232: (version_ == DxfVersion::R13 ? d.annotation_offset : d.block_offset).z = std::atof(gc.value.c_str()); break;
            case 213: d.annotation_offset.x = std::atof(gc.value.c_str()); break;
            case 223: d.annotation_offset.y = std::atof(gc.value.c_str()); break;
            case 233: d.annotation_offset.z = std::atof(gc.value.c_str()); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }

    entity.data = std::move(d);
    return entity;
}

// ---------------------------------------------------------------------------
// SPLINE (2000+)
// ---------------------------------------------------------------------------

// 40 and 41 are lists (knots, weights); 10 starts a control point and 11 a fit point.
// The counts (72–74) follow from the lists.
bool DxfReader::spline_group(SplineData& d, const GroupCode& gc) {
    double v = std::atof(gc.value.c_str());
    switch (gc.code) {
        case 70: d.flags = std::atoi(gc.value.c_str()); return true;
        case 71: d.degree = std::atoi(gc.value.c_str()); return true;
        case 72: case 73: case 74: return true;
        case 42: d.knot_tolerance = v; return true;
        case 43: d.control_tolerance = v; return true;
        case 44: d.fit_tolerance = v; return true;
        case 40: d.knots.push_back(v); return true;
        case 41: d.weights.push_back(v); return true;
        case 10: d.control_points.push_back({v, 0.0, 0.0}); return true;
        case 20: if (!d.control_points.empty()) d.control_points.back().y = v; return true;
        case 30: if (!d.control_points.empty()) d.control_points.back().z = v; return true;
        case 11: d.fit_points.push_back({v, 0.0, 0.0}); return true;
        case 21: if (!d.fit_points.empty()) d.fit_points.back().y = v; return true;
        case 31: if (!d.fit_points.empty()) d.fit_points.back().z = v; return true;
        case 12: d.start_tangent.x = v; return true;
        case 22: d.start_tangent.y = v; return true;
        case 32: d.start_tangent.z = v; return true;
        case 13: d.end_tangent.x = v; return true;
        case 23: d.end_tangent.y = v; return true;
        case 33: d.end_tangent.z = v; return true;
        default: return false;
    }
}

Entity DxfReader::parse_spline() {
    Entity entity;
    entity.type = EntityType::Spline;
    SplineData d;
    d.flags = 0;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        if (spline_group(d, gc)) continue;
        if (gc.code >= 1000) { parse_xdata(entity, gc); }
        else { parse_common_entity_codes(entity, gc); }
    }

    entity.data = std::move(d);
    return entity;
}

// ---------------------------------------------------------------------------
// MESH (2010's, in 2000+ files; plan phase 4, B48): AcDbSubDMesh read in order — its codes
// mean different things where they stand (90 is a face list item, an edge's vertex, the
// override count, a property's type or a transparency)
// ---------------------------------------------------------------------------

Entity DxfReader::parse_mesh() {
    Entity entity;
    entity.type = EntityType::Mesh;
    MeshData d;

    std::vector<GroupCode> g;
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        if (gc.code >= 1000) { parse_xdata(entity, gc); continue; }
        if (subclass_ == "AcDbSubDMesh" && gc.code != 100) g.push_back(gc);
        else parse_common_entity_codes(entity, gc);
    }

    size_t i = 0;
    auto code = [&]() { return i < g.size() ? g[i].code : -1; };
    auto integer = [&]() { return std::atoi(g[i++].value.c_str()); };
    auto num = [&]() { return std::atof(g[i++].value.c_str()); };
    if (code() == 71) d.version = integer();
    if (code() == 72) d.blend_crease = integer() != 0;
    if (code() == 91) d.level = integer();
    int n = code() == 92 ? integer() : 0;
    for (int k = 0; k < n && code() == 10; ++k) {
        Point3 p;
        p.x = num();
        if (code() == 20) p.y = num();
        if (code() == 30) p.z = num();
        d.vertices.push_back(p);
    }
    int list = code() == 93 ? integer() : 0;
    for (int k = 0; k < list && code() == 90;) {
        int count = integer();
        ++k;
        std::vector<int> face;
        for (int v = 0; v < count && k < list && code() == 90; ++v, ++k) face.push_back(integer());
        d.faces.push_back(std::move(face));
    }
    int edges = code() == 94 ? integer() : 0;
    for (int k = 0; k < edges && code() == 90; ++k) {
        MeshEdge e;
        e.start = integer();
        if (code() == 90) e.end = integer();
        d.edges.push_back(e);
    }
    int creases = code() == 95 ? integer() : 0;
    for (int k = 0; k < creases && code() == 140; ++k) d.creases.push_back(num());
    // The overridden sub-entities. A material mapper (type 3), whose form is not known, or
    // anything else this reading does not know: the override data from its count on as read
    size_t start = i;
    bool known = true;
    int subs = code() == 90 ? integer() : 0;
    for (int s = 0; s < subs && known; ++s) {
        if (code() != 91) { known = false; break; }
        MeshOverride o;
        o.marker = integer();
        int properties = code() == 92 ? integer() : 0;
        for (int p = 0; p < properties && known; ++p) {
            if (code() != 90) { known = false; break; }
            MeshProperty prop;
            prop.type = integer();
            switch (prop.type) {
                case 0:
                    if (code() == 63) prop.color.aci = integer();
                    if (code() == 421) prop.color.rgb = integer();
                    if (code() == 431) prop.color.name = g[i++].value;
                    break;
                case 1:
                    if (code() >= 330 && code() <= 369) prop.material_handle = g[i++].value;
                    break;
                case 2:
                    if (code() == 90) prop.transparency.raw = integer();
                    break;
                default:
                    known = false;
                    break;
            }
            o.properties.push_back(prop);
        }
        d.overrides.push_back(std::move(o));
    }
    if (!known || i < g.size()) {
        d.overrides.clear();
        for (i = start; i < g.size(); ++i) d.overrides_unread.push_back({g[i].code, g[i].value});
    }

    entity.data = std::move(d);
    return entity;
}

// ---------------------------------------------------------------------------
// HELIX (2007, B39): AcDbSpline as SPLINE, then AcDbHelix
// ---------------------------------------------------------------------------

Entity DxfReader::parse_helix() {
    Entity entity;
    entity.type = EntityType::Helix;
    HelixData d;
    d.spline.flags = 0;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        if (gc.code >= 1000) { parse_xdata(entity, gc); continue; }
        if (subclass_ == "AcDbSpline" && spline_group(d.spline, gc)) continue;
        if (subclass_ == "AcDbHelix") {
            double v = std::atof(gc.value.c_str());
            int n = std::atoi(gc.value.c_str());
            switch (gc.code) {
                case 90: d.major_version = n; continue;
                case 91: d.maintenance_version = n; continue;
                case 10: d.axis_base.x = v; continue;
                case 20: d.axis_base.y = v; continue;
                case 30: d.axis_base.z = v; continue;
                case 11: d.start.x = v; continue;
                case 21: d.start.y = v; continue;
                case 31: d.start.z = v; continue;
                case 12: d.axis_vector.x = v; continue;
                case 22: d.axis_vector.y = v; continue;
                case 32: d.axis_vector.z = v; continue;
                case 40: d.radius = v; continue;
                case 41: d.turns = v; continue;
                case 42: d.turn_height = v; continue;
                case 290: d.right_handed = n != 0; continue;
                case 280: d.constrain = n; continue;
                default: break;
            }
        }
        parse_common_entity_codes(entity, gc);
    }

    entity.data = std::move(d);
    return entity;
}

// ---------------------------------------------------------------------------
// Surfaces (2007, B39): AcDbModelerGeometry as 3DSOLID, AcDbSurface's isolines, then the
// type's subclass read in order — its codes mean different things where they stand (90 a
// class and a size, 40 a matrix or an angle)
// ---------------------------------------------------------------------------

Entity DxfReader::parse_surface(EntityType type) {
    Entity entity;
    entity.type = type;
    SurfaceData d;
    AcisLineReader sat(d.acis);
    std::vector<GroupCode> body;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        if (gc.code >= 1000) { parse_xdata(entity, gc); continue; }
        if (subclass_ == "AcDbModelerGeometry") {
            if (gc.code == 70) { d.acis.version = std::atoi(gc.value.c_str()); continue; }
            if (gc.code == 290) continue;
            if (gc.code == 2) {
                if (gc.value != "{00000000-0000-0000-0000-000000000000}") d.acis.guid = gc.value;
                continue;
            }
            if (gc.code == 1) { sat.end_line(gc.value, written_size_); continue; }
            if (gc.code == 3) { sat.continue_line(gc.value); continue; }
        } else if (subclass_ == "AcDbSurface") {
            if (gc.code == 71) { d.u_isolines = std::atoi(gc.value.c_str()); continue; }
            if (gc.code == 72) { d.v_isolines = std::atoi(gc.value.c_str()); continue; }
        } else if (subclass_ == "AcDbExtrudedSurface" || subclass_ == "AcDbLoftedSurface" ||
                   subclass_ == "AcDbRevolvedSurface" || subclass_ == "AcDbSweptSurface") {
            if (gc.code != 100) { body.push_back(gc); continue; }
        }
        parse_common_entity_codes(entity, gc);
    }
    sat.finish();

    size_t i = 0;
    auto code = [&]() { return i < body.size() ? body[i].code : -1; };
    auto num = [&]() { return std::atof(body[i++].value.c_str()); };
    auto integer = [&]() { return std::atoi(body[i++].value.c_str()); };
    auto flag = [&]() { return std::atoi(body[i++].value.c_str()) != 0; };
    auto point = [&](int x_code) {
        Point3 p;
        if (code() == x_code) p.x = num();
        if (code() == x_code + 10) p.y = num();
        if (code() == x_code + 20) p.z = num();
        return p;
    };
    auto matrix = [&](int c) {
        std::vector<double> m;
        while (code() == c) m.push_back(num());
        return m;
    };
    // An entity's data: its class (90 or 91), its size in bits (90), the bytes (310, hex)
    auto source = [&](int class_code) {
        SurfaceSource src;
        if (code() == class_code) src.class_id = integer();
        if (code() == 90) src.bits = integer();
        while (code() == 310) {
            const std::string& hex = body[i++].value;
            for (size_t k = 0; k + 1 < hex.size(); k += 2) {
                char pair[3] = {hex[k], hex[k + 1], '\0'};
                src.data.push_back(static_cast<uint8_t>(std::strtoul(pair, nullptr, 16)));
            }
        }
        return src;
    };
    // The sweep's options from 42 to 11 (EXTRUDEDSURFACE, SWEPTSURFACE)
    auto sweep = [&](SurfaceSweep& w) {
        while (i < body.size()) {
            switch (code()) {
                case 42: w.draft_angle = num(); break;
                case 43: w.draft_start = num(); break;
                case 44: w.draft_end = num(); break;
                case 45: w.twist = num(); break;
                case 48: w.scale = num(); break;
                case 49: w.align_angle = num(); break;
                case 46: w.sweep_matrix = matrix(46); break;
                case 47: w.path_matrix = matrix(47); break;
                case 290: w.solid = flag(); break;
                case 70: w.alignment = integer(); break;
                case 71: w.option_71 = integer(); break;
                case 292: w.align_start = flag(); break;
                case 293: w.bank = flag(); break;
                case 294: w.base_point_set = flag(); break;
                case 295: w.sweep_transform_computed = flag(); break;
                case 296: w.path_transform_computed = flag(); break;
                case 11: w.reference_vector = point(11); break;
                default: ++i; break;
            }
        }
    };
    switch (type) {
        case EntityType::ExtrudedSurface:
            d.source = source(90);
            d.sweep_vector = point(10);
            d.matrix = matrix(40);
            sweep(d.sweep);
            break;
        case EntityType::SweptSurface:
            d.source = source(90);
            d.path = source(91);
            d.matrix = matrix(40);
            d.path_entity_matrix = matrix(41);
            sweep(d.sweep);
            break;
        case EntityType::RevolvedSurface:
            d.source = source(90);
            while (i < body.size()) {
                switch (code()) {
                    case 10: d.axis_point = point(10); break;
                    case 11: d.axis_vector = point(11); break;
                    case 40: d.revolve_angle = num(); break;
                    case 41: d.start_angle = num(); break;
                    case 42: d.matrix = matrix(42); break;
                    case 43: d.revolve_draft_angle = num(); break;
                    case 44: d.start_draft_distance = num(); break;
                    case 45: d.end_draft_distance = num(); break;
                    case 46: d.revolve_twist = num(); break;
                    case 290: d.revolve_solid = flag(); break;
                    case 291: d.close_to_axis = flag(); break;
                    default: ++i; break;
                }
            }
            break;
        case EntityType::LoftedSurface:
            d.matrix = matrix(40);
            while (code() == 90) d.curves.push_back(source(90));
            while (i < body.size()) {
                switch (code()) {
                    case 70: d.plane_normal_lofting = integer(); break;
                    case 41: d.start_draft_angle = num(); break;
                    case 42: d.end_draft_angle = num(); break;
                    case 43: d.start_draft_magnitude = num(); break;
                    case 44: d.end_draft_magnitude = num(); break;
                    case 290: d.arc_length_parameters = flag(); break;
                    case 291: d.no_twist = flag(); break;
                    case 292: d.align_direction = flag(); break;
                    case 293: d.simple_surfaces = flag(); break;
                    case 294: d.closed = flag(); break;
                    case 295: d.loft_solid = flag(); break;
                    case 296: d.ruled = flag(); break;
                    case 297: d.virtual_guide = flag(); break;
                    default: ++i; break;
                }
            }
            break;
        default:
            break;
    }

    entity.data = std::move(d);
    return entity;
}

// ---------------------------------------------------------------------------
// HATCH (2000+)
// ---------------------------------------------------------------------------

// The codes mean different things where they stand (10 is the elevation point, a vertex,
// an edge's point or a seed point), so the groups are read first and then in order.
// Groups after the seed points (the gradient, 2004+) stay in the record.
Entity DxfReader::parse_hatch() {
    Entity entity;
    entity.type = EntityType::Hatch;
    HatchData d;

    std::vector<GroupCode> g;
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        if (gc.code >= 1000) { parse_xdata(entity, gc); break; }
        g.push_back(gc);
    }

    size_t i = 0;
    auto code = [&]() { return i < g.size() ? g[i].code : -1; };
    auto num = [&]() { return std::atof(g[i++].value.c_str()); };
    auto integer = [&]() { return std::atoi(g[i++].value.c_str()); };
    auto point = [&](int x_code) {
        Point2 p;
        if (code() == x_code) p.x = num();
        if (code() == x_code + 10) p.y = num();
        return p;
    };

    // Up to the paths: the elevation point, the pattern's name, solid fill, associativity
    int paths = -1;
    while (i < g.size() && paths < 0) {
        switch (code()) {
            case 10: case 20: ++i; break;
            case 30: d.elevation = num(); break;
            case 2:  d.pattern_name = g[i++].value; break;
            case 70: d.solid = integer() != 0; break;
            case 71: d.associative = integer() != 0; break;
            case 91: paths = integer(); break;
            default: parse_common_entity_codes(entity, g[i++]); break;
        }
    }

    for (int p = 0; p < paths && code() == 92; ++p) {
        HatchBoundaryPath path;
        path.flags = integer();
        if (path.is_polyline()) {
            if (code() == 72) ++i;                      // has bulges: follows from them
            if (code() == 73) path.closed = integer() != 0;
            int n = code() == 93 ? integer() : 0;
            for (int k = 0; k < n && code() == 10; ++k) {
                HatchVertex v;
                v.position = point(10);
                if (code() == 42) v.bulge = num();
                path.vertices.push_back(v);
            }
        } else {
            int n = code() == 93 ? integer() : 0;
            for (int k = 0; k < n && code() == 72; ++k) {
                HatchEdge e;
                e.type = static_cast<HatchEdgeType>(integer());
                switch (e.type) {
                    case HatchEdgeType::Line:
                        e.start = point(10);
                        e.end = point(11);
                        break;
                    case HatchEdgeType::Arc:
                    case HatchEdgeType::Ellipse:
                        e.center = point(10);
                        if (e.type == HatchEdgeType::Ellipse) e.major_axis = point(11);
                        if (code() == 40) {
                            if (e.type == HatchEdgeType::Arc) e.radius = num();
                            else e.ratio = num();
                        }
                        if (code() == 50) e.start_angle = num();
                        if (code() == 51) e.end_angle = num();
                        if (code() == 73) e.counterclockwise = integer() != 0;
                        break;
                    case HatchEdgeType::Spline: {
                        if (code() == 94) e.degree = integer();
                        if (code() == 73) e.rational = integer() != 0;
                        if (code() == 74) e.periodic = integer() != 0;
                        if (code() == 95) ++i;
                        if (code() == 96) ++i;
                        while (code() == 40) e.knots.push_back(num());
                        while (code() == 10) {
                            e.control_points.push_back(point(10));
                            if (code() == 42) e.weights.push_back(num());
                        }
                        // 2010+: fit points and their tangents
                        if (version_ >= DxfVersion::R2010 && code() == 97) {
                            int fit = integer();
                            for (int f = 0; f < fit && code() == 11; ++f) e.fit_points.push_back(point(11));
                            e.start_tangent = point(12);
                            e.end_tangent = point(13);
                        }
                        break;
                    }
                }
                path.edges.push_back(std::move(e));
            }
        }
        int sources = code() == 97 ? integer() : 0;
        for (int s = 0; s < sources && code() == 330; ++s) path.source_handles.push_back(g[i++].value);
        d.paths.push_back(std::move(path));
    }

    // The pattern and the seed points
    while (i < g.size()) {
        switch (code()) {
            case 75: d.style = integer(); break;
            case 76: d.pattern_type = integer(); break;
            case 52: d.pattern_angle = num(); break;
            case 41: d.pattern_scale = num(); break;
            case 77: d.pattern_double = integer() != 0; break;
            case 47: d.pixel_size = num(); break;
            case 78: {
                int lines = integer();
                for (int k = 0; k < lines && code() == 53; ++k) {
                    HatchPatternLine line;
                    line.angle = num();
                    if (code() == 43) line.base_point.x = num();
                    if (code() == 44) line.base_point.y = num();
                    if (code() == 45) line.offset.x = num();
                    if (code() == 46) line.offset.y = num();
                    int dashes = code() == 79 ? integer() : 0;
                    for (int s = 0; s < dashes && code() == 49; ++s) line.dashes.push_back(num());
                    d.pattern_lines.push_back(std::move(line));
                }
                break;
            }
            case 98: {
                int seeds = integer();
                for (int k = 0; k < seeds && code() == 10; ++k) d.seed_points.push_back(point(10));
                break;
            }
            case 450: {
                // The gradient (2004+): 450 451 460 461 452 462 453, per color 463 63 421, 470
                HatchGradient gr;
                gr.type = integer();
                bool in = true;
                while (in && i < g.size()) {
                    switch (code()) {
                        case 451: gr.reserved = integer(); break;
                        case 460: gr.angle = num(); break;
                        case 461: gr.shift = num(); break;
                        case 452: gr.single_color = integer() != 0; break;
                        case 462: gr.tint = num(); break;
                        case 453: ++i; break;           // follows from the colors
                        case 463: gr.colors.push_back({num(), Color::by_layer()}); break;
                        case 63:
                            if (gr.colors.empty()) { in = false; break; }
                            gr.colors.back().color.aci = integer();
                            break;
                        case 421:
                            if (gr.colors.empty()) { in = false; break; }
                            gr.colors.back().color.rgb = integer();
                            break;
                        case 470: gr.name = g[i++].value; break;
                        default: in = false; break;
                    }
                }
                d.gradient = std::move(gr);
                break;
            }
            default: parse_common_entity_codes(entity, g[i++]); break;
        }
    }

    entity.data = std::move(d);
    return entity;
}

// ---------------------------------------------------------------------------
// MLINE (2000+)
// ---------------------------------------------------------------------------

// 11 starts a vertex, 74 an element of it; 41 and 42 belong to the element before them.
// The counts (72–75) follow from the lists, except 73 (the style's elements).
Entity DxfReader::parse_mline() {
    Entity entity;
    entity.type = EntityType::Mline;
    MlineData d;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        double v = std::atof(gc.value.c_str());
        MlineVertex* vx = d.vertices.empty() ? nullptr : &d.vertices.back();
        MlineElementParams* el = vx && !vx->elements.empty() ? &vx->elements.back() : nullptr;
        switch (gc.code) {
            case 2:   d.style = gc.value; break;
            case 340: d.style_handle = gc.value; break;
            case 40:  d.scale = v; break;
            case 70:  d.justification = std::atoi(gc.value.c_str()); break;
            case 71:  d.flags = std::atoi(gc.value.c_str()); break;
            case 72:  break;
            case 73:  d.style_elements = std::atoi(gc.value.c_str()); break;
            case 10:  d.start_point.x = v; break;
            case 20:  d.start_point.y = v; break;
            case 30:  d.start_point.z = v; break;
            case 11:  d.vertices.emplace_back(); d.vertices.back().position.x = v; break;
            case 21:  if (vx) vx->position.y = v; break;
            case 31:  if (vx) vx->position.z = v; break;
            case 12:  if (vx) vx->direction.x = v; break;
            case 22:  if (vx) vx->direction.y = v; break;
            case 32:  if (vx) vx->direction.z = v; break;
            case 13:  if (vx) vx->miter.x = v; break;
            case 23:  if (vx) vx->miter.y = v; break;
            case 33:  if (vx) vx->miter.z = v; break;
            case 74:  if (vx) vx->elements.emplace_back(); break;
            case 41:  if (el) el->params.push_back(v); break;
            case 75:  break;
            case 42:  if (el) el->fill_params.push_back(v); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }

    entity.data = std::move(d);
    return entity;
}

// ---------------------------------------------------------------------------
// IMAGE (2000+)
// ---------------------------------------------------------------------------

Entity DxfReader::parse_image() {
    Entity entity;
    entity.type = EntityType::Image;
    ImageData d;

    // 14 starts a clip boundary vertex; 91 (the count) follows from them
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        double v = std::atof(gc.value.c_str());
        int n = std::atoi(gc.value.c_str());
        switch (gc.code) {
            case 90:  d.class_version = n; break;
            case 10:  d.insertion_point.x = v; break;
            case 20:  d.insertion_point.y = v; break;
            case 30:  d.insertion_point.z = v; break;
            case 11:  d.u_vector.x = v; break;
            case 21:  d.u_vector.y = v; break;
            case 31:  d.u_vector.z = v; break;
            case 12:  d.v_vector.x = v; break;
            case 22:  d.v_vector.y = v; break;
            case 32:  d.v_vector.z = v; break;
            case 13:  d.size.x = v; break;
            case 23:  d.size.y = v; break;
            case 340: d.image_def_handle = gc.value; break;
            case 70:  d.display_flags = n; break;
            case 280: d.clipping = n != 0; break;
            case 281: d.brightness = n; break;
            case 282: d.contrast = n; break;
            case 283: d.fade = n; break;
            case 290: d.clip_inside = n != 0; break;
            case 360: d.reactor_handle = gc.value; break;
            case 71:  d.clip_boundary_type = n; break;
            case 91:  break;
            case 14:  d.clip_boundary.push_back({v, 0.0}); break;
            case 24:  if (!d.clip_boundary.empty()) d.clip_boundary.back().y = v; break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }

    entity.data = std::move(d);
    return entity;
}

// ---------------------------------------------------------------------------
// 3DSOLID, REGION, BODY (2000+): the ACIS lines as in the file
// ---------------------------------------------------------------------------

// A SAT line is its 3s and the 1 that ends it: AutoCAD writes a line of 255 characters or more
// as 3s of 255 and the rest (perhaps empty) in 1 (plan phase 5, 5.6; Aspose's conic_pyramid,
// endstop_ztop, kolja_oberteil — the reference has 3 after 1, see write_modeler_geometry())
Entity DxfReader::parse_acis(EntityType type) {
    Entity entity;
    entity.type = type;
    AcisData d;
    AcisLineReader sat(d);

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        switch (gc.code) {
            case 70: d.version = std::atoi(gc.value.c_str()); break;
            case 290: break;                                // 2013+: whether 2 is a GUID
            case 2:                                         // 2013+: the change GUID
                if (subclass_ == "AcDbModelerGeometry") {
                    if (gc.value != "{00000000-0000-0000-0000-000000000000}") d.guid = gc.value;
                } else {
                    parse_common_entity_codes(entity, gc);
                }
                break;
            case 350:                                       // 2007+: the history
                if (subclass_ == "AcDb3dSolid") d.history_handle = gc.value;
                else parse_common_entity_codes(entity, gc);
                break;
            case 1:  sat.end_line(gc.value, written_size_); break;
            case 3:  sat.continue_line(gc.value); break;
            default:
                if (gc.code >= 1000) { parse_xdata(entity, gc); }
                else { parse_common_entity_codes(entity, gc); }
                break;
        }
    }
    sat.finish();

    entity.data = std::move(d);
    return entity;
}

// ---------------------------------------------------------------------------
// MULTILEADER (phase 3, B36): AcDbEntity as for every entity (its proxy graphics, 92 and
// 310, are what the writer makes again), then the groups of AcDbMLeader read by their parts
// (mleader.cpp), and the version out of the XDATA ACAD_MLEADERVER (2000-2007)
// ---------------------------------------------------------------------------

Entity DxfReader::parse_mleader() {
    Entity entity;
    entity.type = EntityType::MLeader;
    std::vector<DxfGroup> body;
    bool in_body = false;

    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        if (gc.code >= 1000) {
            parse_xdata(entity, gc);
            continue;
        }
        if (gc.code == 100) {
            in_body = gc.value == "AcDbMLeader";
            continue;
        }
        if (in_body) body.push_back({gc.code, gc.value});
        else parse_common_entity_codes(entity, gc);
    }

    MLeaderData d;
    internal::parse_mleader(body, d);
    take_mleader_version(entity.props.xdata, d.version);
    entity.data = std::move(d);
    return entity;
}

// ---------------------------------------------------------------------------
// Unknown entity (preserved for round-trip)
// ---------------------------------------------------------------------------

Entity DxfReader::parse_unknown(const std::string& entity_type) {
    Entity entity;
    entity.type = EntityType::Unknown;
    UnknownEntityData d;
    d.entity_type = entity_type;

    GroupCode gc;
    int markers = 0;
    while (read_group(gc)) {
        if (gc.code == 0) { push_back(gc); break; }
        if (gc.code == 100) ++markers;

        // Store common entity codes in both the unknown data and entity props. From R13 on
        // the AcDbEntity ones only in AcDbEntity: further in, a type may use them for
        // something else. 39 and 210 (thickness, extrusion) belong to the type's own
        // subclass.
        bool in_entity = !r13_plus_ || markers <= 1;
        bool head = gc.code == 8 || gc.code == 6 || gc.code == 62 || gc.code == 48 ||
                    gc.code == 60 || gc.code == 67 || gc.code == 5 || gc.code == 370 ||
                    gc.code == 390 || gc.code == 420 || gc.code == 430 || gc.code == 440 ||
                    gc.code == 347;
        bool shape = gc.code == 39 || gc.code == 210 || gc.code == 220 || gc.code == 230;
        if ((head && in_entity) || shape) parse_common_entity_codes(entity, gc);

        d.group_codes.push_back({gc.code, gc.value});
    }

    entity.data = d;
    return entity;
}

} // namespace internal
} // namespace hdxf
