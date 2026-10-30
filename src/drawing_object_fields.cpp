#include "hdxf.hpp"
#include "handles.hpp"
#include <algorithm>
#include <cstdlib>

// The fields DXF 2018 gave objects a place for that the versions before keep elsewhere (plan phase 6,
// B77): the detail and section view styles' display name and its flags (2018: 300 and 90 after 290),
// which up to 2013 are the XRECORD
//   ACAD_XREC_ROUNDTRIP -> 102 DISPLAYNAME, 1 <name>, 102 FLAGS, 90 <flags>
// in the style's extension dictionary — dictionary hard-owned by the style (280 1, 281 1), XRECORD with
// the dictionary as owner and reactor (ACadSharp's sample_AC1027: both styles of Imperial24 and Metric50).

namespace hdxf {

namespace {

const char* const kRoundTrip = "ACAD_XREC_ROUNDTRIP";

uint64_t handle_value(const std::string& h) {
    uint64_t v = 0;
    return internal::parse_handle(h, v) == internal::HandleParse::Ok ? v : 0;
}

bool is_view_style(const Object& o) {
    return o.type == ObjectType::DetailViewStyle || o.type == ObjectType::SectionViewStyle;
}

// The XRECORD of a style's extension dictionary that has its display name: ACAD_XREC_ROUNDTRIP whose
// data starts with 102 DISPLAYNAME
const Object* display_record(const Drawing& dwg, const std::string& xdictionary) {
    if (xdictionary.empty()) return nullptr;
    const Object* dict = dwg.find_object(xdictionary);
    if (!dict || dict->type != ObjectType::Dictionary) return nullptr;
    for (const auto& e : dict->as_dictionary().entries) {
        if (e.name != kRoundTrip) continue;
        const Object* x = dwg.find_object(e.handle);
        if (x && x->type == ObjectType::XRecord && !x->as_xrecord().data.empty() &&
            x->as_xrecord().data[0].code == 102 && x->as_xrecord().data[0].value == "DISPLAYNAME") {
            return x;
        }
    }
    return nullptr;
}

std::vector<DxfGroup> display_data(const ModelDocViewStyleData& d) {
    return {{102, "DISPLAYNAME"}, {1, d.display_name}, {102, "FLAGS"}, {90, std::to_string(d.display_flags)}};
}

// The XRECORD says the style's display name and flags
bool says(const XRecordData& x, const ModelDocViewStyleData& d) {
    const auto& g = x.data;
    return g.size() == 4 && g[0].code == 102 && g[0].value == "DISPLAYNAME" && g[1].code == 1 && g[1].value == d.display_name &&
           g[2].code == 102 && g[2].value == "FLAGS" && g[3].code == 90 && std::atoi(g[3].value.c_str()) == d.display_flags;
}

} // namespace

void Drawing::read_view_style_records() {
    for (auto& o : objects_) {
        if (!is_view_style(o)) continue;
        const Object* x = display_record(*this, o.xdictionary);
        if (!x) continue;
        const auto& g = x->as_xrecord().data;
        ModelDocViewStyleData& d = o.as_view_style();
        for (size_t i = 0; i < g.size(); ++i) {
            if (g[i].code == 1) d.display_name = g[i].value;
            if (g[i].code == 90) d.display_flags = std::atoi(g[i].value.c_str());
        }
    }
}

// Saving: before 2018 the XRECORD says the style's display name and flags (made, changed or, when the
// style is gone, removed with its dictionary — the orphan sweep of sync_roundtrip_records()); in 2018
// the style has them itself and the XRECORD is taken away
void Drawing::sync_view_style_records(const Drawing*) {
    bool older = version_ < DxfVersion::R2018;
    bool scanned = false;
    std::vector<std::string> handles;
    for (auto& o : objects_) {
        if (!is_view_style(o)) continue;
        if (o.handle.empty()) {
            if (!older) continue;
            if (!scanned) {
                scan_handles();
                scanned = true;
            }
            o.handle = next_handle();
        }
        handles.push_back(o.handle);
    }
    auto remove_object = [&](const std::string& handle) {
        uint64_t v = handle_value(handle);
        objects_.erase(std::remove_if(objects_.begin(), objects_.end(), [&](const Object& o) { return handle_value(o.handle) == v; }),
                       objects_.end());
    };
    for (const auto& handle : handles) {
        Object* style = find_object(handle);
        if (!style) continue;
        const ModelDocViewStyleData d = style->as_view_style();
        const Object* found = display_record(*this, style->xdictionary);
        std::string record = found ? found->handle : std::string();
        if (!older) {
            if (record.empty()) continue;
            std::string xdictionary = style->xdictionary;
            Object* dict = find_object(xdictionary);
            auto& entries = dict->as_dictionary().entries;
            entries.erase(std::remove_if(entries.begin(), entries.end(),
                                         [&](const DictionaryEntry& e) { return handle_value(e.handle) == handle_value(record); }),
                          entries.end());
            remove_object(record);
            dict = find_object(xdictionary);
            if (dict && dict->as_dictionary().entries.empty() && dict->xdata.empty() && dict->xdictionary.empty()) {
                remove_object(xdictionary);
                find_object(handle)->xdictionary.clear();
            }
            continue;
        }
        if (!record.empty()) {
            XRecordData& x = find_object(record)->as_xrecord();
            if (!says(x, d)) x.data = display_data(d);
            continue;
        }
        if (!scanned) {
            scan_handles();
            scanned = true;
        }
        std::string xdictionary = style->xdictionary;
        Object* dict = xdictionary.empty() ? nullptr : find_object(xdictionary);
        if (dict && dict->type != ObjectType::Dictionary) continue;             // not a dictionary: left alone
        if (!dict) {
            Object dictionary;
            dictionary.type = ObjectType::Dictionary;
            dictionary.handle = next_handle();
            dictionary.owner = handle;
            DictionaryData dd;
            dd.hard_owner = true;
            dictionary.data = dd;
            xdictionary = dictionary.handle;
            objects_.push_back(std::move(dictionary));
            find_object(handle)->xdictionary = xdictionary;
        }
        Object x;
        x.type = ObjectType::XRecord;
        x.handle = next_handle();
        x.owner = xdictionary;
        x.reactors = {xdictionary};
        XRecordData data;
        data.data = display_data(d);
        x.data = data;
        std::string x_handle = x.handle;
        objects_.push_back(std::move(x));
        find_object(xdictionary)->as_dictionary().entries.push_back({kRoundTrip, x_handle, true});
    }
}

} // namespace hdxf
