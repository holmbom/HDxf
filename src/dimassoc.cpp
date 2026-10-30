#include "ContextDimensionTypes.hpp"
#include "DynamicBlockTypes.hpp"
#include "dxf_writer.hpp"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

// DIMASSOC of step 9.17 (phase 9; plan 2026-10-27-fas-9-typer.md, B100-B107, B112-B116): the association of a dimension with the
// geometry it measures (AcDbDimAssoc). AutoCAD's files (doc/facit/fas9_special_dimensions.scr and Erik's
// acad_<version>_dxf_dimensions*.dxf, 2000 … 2018): 330 the dimension, 90 the mask of the points (1, 2, 4, 8), 70, 71, then for each
// point of the mask its AcDbOsnapPointRef (1 72 331+ 73 91 [301] 40 10 20 30 [332+ 74 92 302] 75). The DIMASSOC is in the dimension's
// extension dictionary (ACAD_DIMASSOC), is a reactor of the dimension and of each geometry it refers to (their back links).

namespace hdxf {

namespace {

bool same_handle(const std::string& a, const std::string& b) {
    auto strip = [](const std::string& s) {
        size_t i = 0;
        while (i + 1 < s.size() && s[i] == '0') ++i;
        std::string out = s.substr(i);
        for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return out;
    };
    return !a.empty() && !b.empty() && strip(a) == strip(b);
}

const char* kDimAssoc = "ACAD_DIMASSOC";

} // namespace

const Object* Drawing::dimension_association(const std::string& dimension_handle) const {
    const Entity* e = find_entity(dimension_handle);
    if (!e || e->props.xdictionary.empty()) return nullptr;
    const Object* dict = find_object(e->props.xdictionary);
    if (!dict || dict->type != ObjectType::Dictionary) return nullptr;
    for (const DictionaryEntry& entry : dict->as_dictionary().entries) {
        if (entry.name != kDimAssoc) continue;
        const Object* o = find_object(entry.handle);
        if (o && o->type == ObjectType::DimAssoc) return o;
    }
    return nullptr;
}

Object* Drawing::add_dimension_association(const std::string& dimension_handle, DimAssocData data) {
    Entity* dim = const_cast<Entity*>(find_entity(dimension_handle));
    if (!dim || !dim->dimension_part()) return nullptr;
    if (version_ >= DxfVersion::R2000) ensure_r2000_structure();
    else if (version_ >= DxfVersion::R13) ensure_older_structure();
    scan_handles();
    dim = const_cast<Entity*>(find_entity(dimension_handle));
    remove_dimension_association(dimension_handle);
    dim = const_cast<Entity*>(find_entity(dimension_handle));

    if (dim->props.xdictionary.empty() || !find_object(dim->props.xdictionary)) {
        Object d;
        d.type = ObjectType::Dictionary;
        d.handle = next_handle();
        d.owner = dim->props.handle;
        DictionaryData dd;
        dd.hard_owner = true;
        d.data = dd;
        dim->props.xdictionary = d.handle;
        objects_.push_back(std::move(d));
    }
    const std::string dictionary = dim->props.xdictionary;
    data.dimension = dim->props.handle;
    int32_t mask = data.flags & ~15;
    for (size_t i = 0; i < data.points.size(); ++i) {
        if (data.points[i]) mask |= 1 << i;
    }
    data.flags = mask;
    Object o;
    o.type = ObjectType::DimAssoc;
    o.handle = next_handle();
    o.owner = dictionary;
    o.reactors = {dictionary};
    o.data = Boxed<DimAssocData>(std::move(data));
    const std::string handle = o.handle;
    objects_.push_back(std::move(o));
    find_object(dictionary)->as_dictionary().entries.push_back({kDimAssoc, handle, true});
    return find_object(handle);
}

bool Drawing::remove_dimension_association(const std::string& dimension_handle) {
    const Object* found = dimension_association(dimension_handle);
    if (!found) return false;
    const std::string handle = found->handle;
    const Entity* dim = find_entity(dimension_handle);
    if (Object* dict = find_object(dim->props.xdictionary); dict && dict->type == ObjectType::Dictionary) {
        auto& entries = dict->as_dictionary().entries;
        entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const DictionaryEntry& e) { return same_handle(e.handle, handle); }),
                      entries.end());
    }
    objects_.erase(std::remove_if(objects_.begin(), objects_.end(), [&](const Object& o) { return same_handle(o.handle, handle); }),
                   objects_.end());
    return true;
}

namespace internal {

namespace {

bool read_point_ref(GroupCursor& c, DimAssocPointRef& p) {
    c.require_string(1, p.class_name);
    c.require_integer(72, p.osnap_type);
    while (c.ok() && c.code() == 331) {
        std::string h;
        c.handle(331, h);
        p.main_handles.push_back(h);
    }
    c.require_integer(73, p.main_subentity_type);
    c.require_integer(91, p.main_marker);
    c.string(301, p.xref_handle_string);
    c.require_real(40, p.near_parameter);
    c.require_point3(10, p.point);
    while (c.ok() && c.code() == 332) {
        std::string h;
        c.handle(332, h);
        p.intersection_handles.push_back(h);
    }
    int16_t t = 0;
    if (c.integer(74, t)) p.intersection_subentity_type = t;
    int32_t m = 0;
    if (c.integer(92, m)) p.intersection_marker = m;
    c.string(302, p.intersection_xref);
    c.require_integer(75, p.last_point_ref);
    return c.ok();
}

bool read_dim_assoc(const RecordView& view, TypedReadContext& ctx, Object& o) {
    DimAssocData d;
    if (!read_subclass(view, ctx, "AcDbDimAssoc", [&](GroupCursor& c) {
            c.handle(330, d.dimension);
            c.require_integer(90, d.flags);
            c.require_integer(70, d.trans_space);
            c.require_integer(71, d.rotated_type);
            for (size_t i = 0; i < d.points.size() && c.ok(); ++i) {
                if (!(d.flags & (1 << i))) continue;
                DimAssocPointRef p;
                if (!read_point_ref(c, p)) return;
                d.points[i] = std::move(p);
            }
        })) {
        return false;
    }
    o.data = Boxed<DimAssocData>(std::move(d));
    return true;
}

void dim_assoc_references(const Object& o, std::vector<TypedReference>& out) {
    const DimAssocData& d = o.as_dim_assoc();
    // The dimension and the geometry get the DIMASSOC as a reactor (AutoCAD's own files)
    if (!d.dimension.empty() && d.dimension != "0") out.push_back({d.dimension, RefKind::SoftPointer, true});
    for (const auto& slot : d.points) {
        if (!slot) continue;
        for (const std::string& h : slot->main_handles) {
            if (!h.empty() && h != "0") out.push_back({h, RefKind::SoftPointer, true});
        }
        for (const std::string& h : slot->intersection_handles) {
            if (!h.empty() && h != "0") out.push_back({h, RefKind::SoftPointer, true});
        }
    }
}

// A DIMASSOC none of whose points has geometry left is gone with it (AutoCAD: erasing the circle a radius dimension measures
// leaves the dimension and no DIMASSOC, fas9_special_dimensions.scr: dim_special circle_erased)
bool dim_assoc_dropped(const Object& o, const std::function<bool(const std::string&)>& dead) {
    const DimAssocData& d = o.as_dim_assoc();
    bool any = false;
    for (const auto& slot : d.points) {
        if (!slot) continue;
        any = true;
        for (const std::string& h : slot->main_handles) {
            if (!dead(h)) return false;
        }
        for (const std::string& h : slot->intersection_handles) {
            if (!dead(h)) return false;
        }
    }
    return any;
}

} // namespace

void register_dimassoc_codecs(TypedRegistry& registry) {
    ObjectCodec c;
    c.dxf_name = "DIMASSOC";
    c.type = ObjectType::DimAssoc;
    c.read = read_dim_assoc;
    c.write = write_dim_assoc;
    c.references = dim_assoc_references;
    c.dropped_when = dim_assoc_dropped;
    c.cls.cpp_name = "AcDbDimAssoc";
    c.cls.application = "\"AcDbDimAssoc|Product Desc:     AcDim ARX App For Dimension|Company:          Autodesk, Inc.|WEB Address:      www.autodesk.com\"";
    c.cls.proxy_flags = 0;
    c.cls.in_older = false;
    // AutoCAD's files of 2000 … 2018 (ASCII and binary) have it; R12 has no OBJECTS section; R13 and R14: no file, no claim
    VersionMatrix m = VersionMatrix::unestablished();
    m.set(DxfVersion::R12, Generation::None, ErrorBasis::Documented);
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013,
                         DxfVersion::R2018}) {
        m.set(v, Generation::Native);
        m.autocad(v, AutoCadStatus::Verified);
    }
    c.versions = m;
    // The dimension (330) is a pointer; the geometry (331, 332) is read per point, the points are one list (a point that loses
    // its geometry takes its groups and its bit of the mask with it)
    RefDescriptor dimension;
    dimension.subclass = "AcDbDimAssoc";
    dimension.occurrence = 0;
    dimension.code = 330;
    dimension.ordinal = 0;
    dimension.kind = RefKind::SoftPointer;
    dimension.removal = RefRemoval::ZeroInPlace;
    c.refs = {dimension};
    ListSpec all;
    all.subclass = "AcDbDimAssoc";
    all.occurrence = 0;
    all.id = 1;
    all.shape = ListShape::WholeSubclass;
    c.lists = {all};
    c.step = 17;
    registry.add(std::move(c));
}

} // namespace internal
} // namespace hdxf
