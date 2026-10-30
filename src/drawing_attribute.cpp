#include "hdxf.hpp"
#include "attribute_mirror.hpp"
#include "handles.hpp"
#include "mtext_data.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

// Multiline attributes before 2018 (plan phase 6, B75, B76): the text is in the XRECORD ACAD_MLATT
// of the attribute's extension dictionary (with ACAD_MATT_HEIGHT beside it), the attribute's own
// groups are its first line as TEXT and the other lines are ATTRIBs of their own — the secondary
// attributes — whose handles the XRECORD lists (ACadSharp's sample_AC1027 and ODA's chain of
// AutoCAD's core, test/data/README.md). In 2018 the text is in the record itself.

namespace hdxf {

namespace {

uint64_t handle_value(const std::string& h) {
    uint64_t v = 0;
    return internal::parse_handle(h, v) == internal::HandleParse::Ok ? v : 0;
}

const char* const kMlatt = "ACAD_MLATT";
const char* const kRoundTrip = "ACAD_XREC_ROUNDTRIP";
const char* const kMattHeight = "ACAD_MATT_HEIGHT";

// The fields of an attribute its XRECORD and its other lines come from
std::string key_of(const MultilineAttribute& ml, const std::string& tag) {
    std::ostringstream o;
    o.precision(17);
    const MTextData& t = ml.text;
    o << ml.type << ';' << ml.really_locked << ';' << ml.annotation_scale << ';' << ml.tag << ';' << tag << ';'
      << ml.alignment_point.x << ',' << ml.alignment_point.y << ',' << ml.alignment_point.z << ';' << t.insertion_point.x << ','
      << t.insertion_point.y << ',' << t.insertion_point.z << ';' << t.height << ';' << t.reference_width << ';' << t.defined_height
      << ';' << t.attachment << ';' << t.drawing_direction << ';' << t.style << ';' << t.x_axis_direction.x << ','
      << t.x_axis_direction.y << ',' << t.x_axis_direction.z << ';' << t.line_spacing_style << ';' << t.line_spacing_factor << ';'
      << t.background.flags << ';' << t.background.scale << ';' << t.background.color.aci << ',' << t.background.color.rgb << ','
      << t.background.color.name << ';' << t.background.transparency.raw << ';' << t.content;
    return o.str();
}

// The record `name` of an extension dictionary (ACAD_MLATT, or ACAD_XREC_ROUNDTRIP whose data starts with 102 ACAD_MATT_HEIGHT)
const Object* entry_of(const Drawing& dwg, const std::string& xdictionary, const char* name) {
    if (xdictionary.empty()) return nullptr;
    const Object* dict = dwg.find_object(xdictionary);
    if (!dict || dict->type != ObjectType::Dictionary) return nullptr;
    for (const auto& e : dict->as_dictionary().entries) {
        if (e.name != name) continue;
        const Object* x = dwg.find_object(e.handle);
        if (x && x->type == ObjectType::XRecord) {
            if (std::string(name) == kRoundTrip) {
                const auto& data = x->as_xrecord().data;
                if (data.empty() || data[0].code != 102 || data[0].value != kMattHeight) continue;
            }
            return x;
        }
    }
    return nullptr;
}

} // namespace

// Reading: each secondary attribute is told whose it is
void Drawing::link_multiline_attributes() {
    auto link = [](std::vector<Entity>& list) {
        for (auto& e : list) {
            if (e.type != EntityType::Insert) continue;
            auto& attributes = e.as_insert().attributes;
            for (auto& a : attributes) {
                if (!a.data.multiline) continue;
                for (const auto& h : a.data.multiline->secondary) {
                    for (auto& b : attributes) {
                        if (handle_value(b.props.handle) == handle_value(h) && b.data.secondary_of.empty()) {
                            b.data.secondary_of = a.props.handle;
                        }
                    }
                }
            }
        }
    };
    link(entities_);
    for (auto& b : blocks_) link(b.entities);
}

// Saving: before 2018 the XRECORDs of a multiline attribute (made, updated when the text, the tag or
// the attribute's other fields changed since it was read, or removed) and the ATTRIBs of the text's
// other lines; from 2018 none of them (the text goes into the record itself)
void Drawing::sync_multiline_attributes(const Drawing* original) {
    bool older = version_ < DxfVersion::R2018;
    scan_handles();
    std::unique_ptr<internal::TextLayout> layout;
    auto layout_of = [&]() -> internal::TextLayout& {
        if (!layout) layout = std::make_unique<internal::TextLayout>(*this);
        return *layout;
    };

    // The attributes as they were read, by handle
    std::unordered_map<uint64_t, std::string> original_keys;
    if (original) {
        auto note = [&](const std::string& handle, const OptionalBoxed<MultilineAttribute>& ml, const std::string& tag) {
            if (ml) original_keys[handle_value(handle)] = key_of(*ml, tag);
        };
        auto scan = [&](const std::vector<Entity>& list) {
            for (const auto& e : list) {
                if (e.type == EntityType::Attdef) note(e.props.handle, e.as_attdef().multiline, e.as_attdef().tag);
                if (e.type == EntityType::Attrib) note(e.props.handle, e.as_attrib().multiline, e.as_attrib().tag);
                if (e.type != EntityType::Insert) continue;
                for (const auto& a : e.as_insert().attributes) note(a.props.handle, a.data.multiline, a.data.tag);
            }
        };
        scan(original->entities_);
        for (const auto& b : original->blocks_) scan(b.entities);
    }

    auto remove_object = [&](const std::string& handle) {
        uint64_t v = handle_value(handle);
        objects_.erase(std::remove_if(objects_.begin(), objects_.end(),
                                      [&](const Object& o) { return handle_value(o.handle) == v; }),
                       objects_.end());
    };

    // The extension dictionary of a carrier: the XRECORDs it should have (`want`) with `data`
    auto records = [&](std::string& owner, std::string& xdictionary, bool want, const std::vector<DxfGroup>& mlatt,
                       const std::vector<DxfGroup>& height, bool update) {
        const Object* found = entry_of(*this, xdictionary, kMlatt);
        const Object* found_height = entry_of(*this, xdictionary, kRoundTrip);
        if (!want) {
            std::vector<std::string> handles;
            if (found) handles.push_back(found->handle);
            if (found_height) handles.push_back(found_height->handle);
            for (const std::string& handle : handles) {
                Object* dict = find_object(xdictionary);
                auto& entries = dict->as_dictionary().entries;
                entries.erase(std::remove_if(entries.begin(), entries.end(),
                                             [&](const DictionaryEntry& e) { return handle_value(e.handle) == handle_value(handle); }),
                              entries.end());
                remove_object(handle);
            }
            if (!handles.empty() && !xdictionary.empty()) {
                Object* dict = find_object(xdictionary);
                if (dict && dict->as_dictionary().entries.empty() && dict->xdata.empty() && dict->xdictionary.empty()) {
                    remove_object(xdictionary);
                    xdictionary.clear();
                }
            }
            return;
        }
        if (found && found_height && !update) return;
        if (owner.empty()) owner = next_handle();
        Object* dict = xdictionary.empty() ? nullptr : find_object(xdictionary);
        if (dict && dict->type != ObjectType::Dictionary) return;           // not a dictionary: left alone
        if (!dict) {
            Object dictionary;
            dictionary.type = ObjectType::Dictionary;
            dictionary.handle = next_handle();
            dictionary.owner = owner;
            DictionaryData d;
            d.hard_owner = true;
            dictionary.data = d;
            xdictionary = dictionary.handle;
            objects_.push_back(std::move(dictionary));
        }
        auto make = [&](const char* name, const std::vector<DxfGroup>& data) {
            Object x;
            x.type = ObjectType::XRecord;
            x.handle = next_handle();
            x.owner = xdictionary;
            x.reactors = {xdictionary};
            XRecordData d;
            d.data = data;
            x.data = d;
            std::string handle = x.handle;
            objects_.push_back(std::move(x));
            find_object(xdictionary)->as_dictionary().entries.push_back({name, handle, true});
        };
        std::string found_handle = found ? found->handle : std::string();
        std::string found_height_handle = found_height ? found_height->handle : std::string();
        if (!found_handle.empty()) find_object(found_handle)->as_xrecord().data = mlatt;
        else make(kMlatt, mlatt);
        if (!found_height_handle.empty()) find_object(found_height_handle)->as_xrecord().data = height;
        else make(kRoundTrip, height);
    };

    auto sync_list = [&](std::vector<Entity>& list) {
        for (size_t i = 0; i < list.size(); ++i) {
            Entity& e = list[i];
            // ---- The definition and a stand-alone attribute: the XRECORDs
            if (e.type == EntityType::Attdef || e.type == EntityType::Attrib) {
                OptionalBoxed<MultilineAttribute>& ml = e.type == EntityType::Attdef ? e.as_attdef().multiline : e.as_attrib().multiline;
                const std::string& tag = e.type == EntityType::Attdef ? e.as_attdef().tag : e.as_attrib().tag;
                bool want = older && ml && ml->has_text;
                if (want || !e.props.xdictionary.empty()) {
                    bool update = true;
                    if (want) {
                        auto it = original_keys.find(handle_value(e.props.handle));
                        update = it == original_keys.end() || it->second != key_of(*ml, tag);
                    }
                    if (e.props.handle.empty() && want) e.props.handle = next_handle();
                    records(e.props.handle, e.props.xdictionary, want,
                            want ? internal::mlatt_xrecord_data(*ml, tag) : std::vector<DxfGroup>(),
                            want ? internal::matt_height_xrecord_data(*ml) : std::vector<DxfGroup>(), update);
                }
                continue;
            }
            if (e.type != EntityType::Insert) continue;
            // ---- The attributes of an INSERT: the XRECORDs and the other lines
            auto& attributes = e.as_insert().attributes;
            for (size_t j = 0; j < attributes.size(); ++j) {
                InsertAttribute& a = attributes[j];
                if (!a.data.secondary_of.empty()) continue;          // another line, below
                bool want = older && a.data.multiline && a.data.multiline->has_text;
                if (!want && a.data.multiline) {
                    // 2018 (or a text taken away): the other lines go
                    auto& secondary = a.data.multiline->secondary;
                    if (!secondary.empty()) {
                        std::string primary = a.props.handle;
                        attributes.erase(std::remove_if(attributes.begin(), attributes.end(), [&](const InsertAttribute& b) {
                            return !b.data.secondary_of.empty() && handle_value(b.data.secondary_of) == handle_value(primary);
                        }), attributes.end());
                        for (j = 0; j < attributes.size() && handle_value(attributes[j].props.handle) != handle_value(primary); ++j) {}
                        if (j == attributes.size()) break;
                        attributes[j].data.multiline->secondary.clear();
                    }
                }
                InsertAttribute& primary = attributes[j];
                if (!want) {
                    if (!primary.props.xdictionary.empty()) {
                        records(primary.props.handle, primary.props.xdictionary, false, {}, {}, false);
                    }
                    continue;
                }
                if (primary.props.handle.empty()) primary.props.handle = next_handle();
                MultilineAttribute& ml = *primary.data.multiline;
                const std::string parent = primary.props.handle;
                auto it = original_keys.find(handle_value(parent));
                bool changed = it == original_keys.end() || it->second != key_of(ml, primary.data.tag);

                // The other lines
                // (a scaled or rotated reference has an attribute for each run of the text)
                std::vector<internal::AttributeLine> lines = internal::attribute_lines(
                    ml, primary.props.extrusion, layout_of(), internal::attributes_by_runs(e.as_insert()));
                size_t needed = ml.type == 2 && lines.size() > 1 ? lines.size() - 1 : 0;
                std::vector<size_t> existing;
                for (size_t k = 0; k < attributes.size(); ++k) {
                    if (!attributes[k].data.secondary_of.empty() && handle_value(attributes[k].data.secondary_of) == handle_value(parent)) existing.push_back(k);
                }
                // The XRECORD's references are the attribute's other lines. A text that did not change
                // keeps the fragments it was read with (AutoCAD splits a line wherever its format
                // changes, so their number is not the number of lines) as long as the references
                // find them; they are made again only for a changed text
                bool references_match = existing.size() == ml.secondary.size();
                for (size_t k = 0; references_match && k < existing.size(); ++k) {
                    references_match = handle_value(attributes[existing[k]].props.handle) == handle_value(ml.secondary[k]);
                }
                bool consistent = references_match && existing.size() == needed;
                if ((changed || !references_match) && (!consistent || (changed && needed > 0))) {
                    std::vector<InsertAttribute> made;
                    std::vector<std::string> handles;
                    // The other lines run along the text's direction, as the first one does
                    double text_rotation = internal::attribute_mirror(ml, primary.data.tag, false, version_, primary.props.extrusion,
                                                                      layout_of()).rotation;
                    for (size_t k = 0; k < needed; ++k) {
                        InsertAttribute b;
                        if (consistent) {
                            b = attributes[existing[k]];
                        } else {
                            b.props = primary.props;
                            b.props.handle = next_handle();
                            b.props.xdictionary.clear();
                            b.props.reactors.clear();
                            b.props.xdata.clear();
                            b.raw = RawRecord();
                        }
                        char suffix[16];
                        std::snprintf(suffix, sizeof(suffix), "_%03d", static_cast<int>(k) + 2);
                        const internal::AttributeLine& line = lines[k + 1];
                        AttribData d = primary.data;
                        d.multiline.reset();
                        d.tag = primary.data.tag + suffix;
                        d.value = line.text;
                        d.start_point = line.pen;
                        d.alignment_point = line.pen;
                        d.h_justify = 0;
                        d.v_justify = 0;
                        d.height = line.height;
                        d.style = ml.text.style;
                        d.lock_position = true;
                        d.rotation = text_rotation;
                        d.secondary_of = parent;
                        b.data = std::move(d);
                        handles.push_back(b.props.handle);
                        made.push_back(std::move(b));
                    }
                    if (consistent) {
                        for (size_t k = 0; k < needed; ++k) attributes[existing[k]] = std::move(made[k]);
                    } else {
                        std::sort(existing.begin(), existing.end(), std::greater<size_t>());
                        for (size_t k : existing) {
                            attributes.erase(attributes.begin() + static_cast<long>(k));
                            if (k < j) --j;
                        }
                        attributes.insert(attributes.begin() + static_cast<long>(j) + 1, std::make_move_iterator(made.begin()),
                                          std::make_move_iterator(made.end()));
                    }
                    attributes[j].data.multiline->secondary = handles;
                }
                InsertAttribute& now = attributes[j];
                records(now.props.handle, now.props.xdictionary, true,
                        internal::mlatt_xrecord_data(*now.data.multiline, now.data.tag),
                        internal::matt_height_xrecord_data(*now.data.multiline), changed || !references_match);
            }
            // What no attribute points to any more (its first line gone, its text taken away)
            std::unordered_set<uint64_t> owned;
            for (const auto& a : attributes) {
                if (a.data.multiline) for (const auto& h : a.data.multiline->secondary) owned.insert(handle_value(h));
            }
            attributes.erase(std::remove_if(attributes.begin(), attributes.end(), [&](const InsertAttribute& a) {
                return !a.data.secondary_of.empty() && !owned.count(handle_value(a.props.handle));
            }), attributes.end());
        }
    };
    sync_list(entities_);
    for (auto& b : blocks_) sync_list(b.entities);

    // An extension dictionary with the XRECORD of an attribute that is gone goes with it
    std::unordered_set<uint64_t> owners;
    auto owners_of = [&](const std::vector<Entity>& list) {
        for (const auto& e : list) {
            if (!e.props.xdictionary.empty()) owners.insert(handle_value(e.props.xdictionary));
            if (e.type != EntityType::Insert) continue;
            for (const auto& a : e.as_insert().attributes) {
                if (!a.props.xdictionary.empty()) owners.insert(handle_value(a.props.xdictionary));
            }
        }
    };
    owners_of(entities_);
    for (const auto& b : blocks_) {
        owners_of(b.entities);
        if (!b.record.xdictionary.empty()) owners.insert(handle_value(b.record.xdictionary));
    }
    for (const auto& l : linetypes_) if (!l.xdictionary.empty()) owners.insert(handle_value(l.xdictionary));
    for (const auto& o : objects_) if (!o.xdictionary.empty()) owners.insert(handle_value(o.xdictionary));
    std::vector<std::string> orphans;
    for (const auto& o : objects_) {
        if (o.type != ObjectType::Dictionary || owners.count(handle_value(o.handle))) continue;
        for (const auto& e : o.as_dictionary().entries) {
            if (e.name != kMlatt) continue;
            const Object* x = find_object(e.handle);
            if (x && x->type == ObjectType::XRecord && x->owner == o.handle) orphans.push_back(o.handle);
        }
    }
    for (const auto& dict_handle : orphans) {
        Object* dict = find_object(dict_handle);
        if (!dict) continue;
        std::vector<std::string> gone;
        for (const auto& e : dict->as_dictionary().entries) gone.push_back(e.handle);
        for (const auto& h : gone) {
            const Object* x = find_object(h);
            if (x && x->type == ObjectType::XRecord && x->owner == dict_handle) remove_object(h);
        }
        remove_object(dict_handle);
    }
}

} // namespace hdxf
