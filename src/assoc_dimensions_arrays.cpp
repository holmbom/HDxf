#include "AssocExtended.hpp"

// The dimension, multileader and array action bodies (phase 9, step 9.15; plan 2026-10-27-fas-9-typer.md, F9.12). AutoCAD
// 2025's own files (doc/facit/fas9_assoc_extended.scr in 2000 … 2018, ASCII and binary; the 9.10 aligned dimension of
// Autodesk's example drawing) have them as:
//   a dimension body   AcDbAssocActionBody 90 version and, in version 2 (2013 and later), 90 and 330 (the dimension's
//                      dependency); in version 1 (up to 2010) AcDbAssocParamBasedActionBody carries the parameters and then
//                      the same 90 and 330. Then the type's own subclass, whose name is AutoCAD's upper-case one
//                      (ACDBASSOCROTATEDDIMACTIONBODY, ...ALIGNEDDIM..., ...3POINTANGULARDIM..., ...DIAMETRICDIM...,
//                      ...RADIALDIM..., ...MLEADER...; the ordinate dimension's is named ACDBASSOC3POINTANGULARDIMACTIONBODY):
//                      90 version and the pointers (330) to its osnap parameters: rotated and aligned two, the three-point
//                      angular three, the ordinate two (the first 0 when the feature is the origin), diametric and radial one;
//                      the multileader has 90 version, 90, 90 and one 330
//   an array body      AcDbAssocActionBody, AcDbAssocArrayActionBody (90 version, 1 the class of the array's parameters,
//                      90 the parameters' version, 90 the number of items, 1 the items' class), one AcDbAssocArrayItem
//                      subclass per item (90 version, 90 item, 90 row, 90 level, 90 flags, then a point (11) or, when flag 4
//                      is set, a 4 x 4 matrix of 16 values (40), then 330 for the item's object and, when flag 16 is set, a
//                      second 330 for the item's modify body) and 16 values (40) after the last item: the array's own
//                      placement. The array's rows, counts and spacing are the value parameters of its action
//                      (AssocActionBase::value_params). The modify body has AcDbAssocArrayModifyActionBody (5 x 90) after
//                      its array part

namespace hdxf {

const char* assoc_array_parameters_class(AssocArrayKind kind) {
    switch (kind) {
        case AssocArrayKind::Rectangular: return "AcDbAssocArrayRectangularParameters";
        case AssocArrayKind::Polar: return "AcDbAssocArrayPolarParameters";
        case AssocArrayKind::Path: return "AcDbAssocArrayPathParameters";
        case AssocArrayKind::Modify: return "AcDbAssocArrayModifyParameters";
    }
    return "AcDbAssocArrayRectangularParameters";
}

bool assoc_array_kind_of(const std::string& class_name, AssocArrayKind& kind) {
    for (AssocArrayKind k : {AssocArrayKind::Rectangular, AssocArrayKind::Polar, AssocArrayKind::Path, AssocArrayKind::Modify}) {
        if (class_name == assoc_array_parameters_class(k)) {
            kind = k;
            return true;
        }
    }
    return false;
}

namespace internal {

namespace {

template <typename Data>
Data& data_of(Object& o) { return *o.data.get<Boxed<Data>>(); }
template <typename Data>
const Data& data_of(const Object& o) { return *o.data.get<Boxed<Data>>(); }

// ---------------------------------------------------------------------------
// The dimension bodies
// ---------------------------------------------------------------------------

bool read_dimension_body(const RecordView& view, TypedReadContext& ctx, AssocDimensionBodyBase& b, const char* subclass,
                         std::initializer_list<std::string*> pointers, std::initializer_list<int32_t*> extra_integers = {}) {
    auto tail = [&](GroupCursor& c) {
        c.require_integer(90, b.dimension_flag);
        c.require_handle(330, b.dimension_dependency);
        return c.ok();
    };
    if (!read_assoc_action_body_head(view, ctx, b.head, tail)) return false;
    GroupCursor c(view, subclass);
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no " + subclass + " subclass");
    c.require_integer(90, b.version);
    for (int32_t* v : extra_integers) c.require_integer(90, *v);
    for (std::string* p : pointers) c.require_handle(330, *p);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    return true;
}

void write_dimension_body(RecordWriter& w, const AssocDimensionBodyBase& b, const char* subclass,
                          std::initializer_list<const std::string*> pointers, std::initializer_list<int32_t> extra_integers = {}) {
    write_assoc_action_body_head(w, b.head, [&] {
        w.integer(90, b.dimension_flag);
        w.handle(330, assoc_pointer(w, b.dimension_dependency));
    });
    w.subclass(subclass);
    w.integer(90, b.version);
    for (int32_t v : extra_integers) w.integer(90, v);
    for (const std::string* p : pointers) w.handle(330, assoc_pointer(w, *p));
}

void dimension_references(const AssocDimensionBodyBase& b, std::initializer_list<const std::string*> pointers,
                          std::vector<TypedReference>& out) {
    assoc_body_references(b.head, out);
    assoc_add_reference(out, b.dimension_dependency, RefKind::SoftPointer);
    for (const std::string* p : pointers) assoc_add_reference(out, *p, RefKind::SoftPointer);
}

std::vector<RefDescriptor> dimension_descriptors(const char* subclass) {
    std::vector<RefDescriptor> refs = assoc_body_descriptors();
    refs.push_back(assoc_ref("AcDbAssocActionBody", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace));
    refs.push_back(assoc_ref(subclass, 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace));
    return refs;
}

constexpr const char* kRotated = "ACDBASSOCROTATEDDIMACTIONBODY";
constexpr const char* k3Point = "ACDBASSOC3POINTANGULARDIMACTIONBODY";
constexpr const char* kOrdinateSubclass = "ACDBASSOC3POINTANGULARDIMACTIONBODY";      // AutoCAD names the ordinate body's own subclass so
constexpr const char* kDiametric = "ACDBASSOCDIAMETRICDIMACTIONBODY";
constexpr const char* kRadial = "ACDBASSOCRADIALDIMACTIONBODY";
constexpr const char* kMLeader = "ACDBASSOCMLEADERACTIONBODY";

bool read_rotated(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocRotatedDimActionBodyData d;
    if (!read_dimension_body(view, ctx, d.body, kRotated, {&d.first_pointer, &d.second_pointer})) return false;
    o.data = Boxed<AssocRotatedDimActionBodyData>(std::move(d));
    return true;
}
void write_rotated(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocRotatedDimActionBodyData>(o);
    w.object_head("ACDBASSOCROTATEDDIMACTIONBODY", o);
    write_dimension_body(w, d.body, kRotated, {&d.first_pointer, &d.second_pointer});
    w.xdata(o.xdata);
}
void rotated_references(const Object& o, std::vector<TypedReference>& out) {
    const auto& d = data_of<AssocRotatedDimActionBodyData>(o);
    dimension_references(d.body, {&d.first_pointer, &d.second_pointer}, out);
}

bool read_3point(const RecordView& view, TypedReadContext& ctx, Object& o) {
    Assoc3PointAngularDimActionBodyData d;
    if (!read_dimension_body(view, ctx, d.body, k3Point, {&d.first_pointer, &d.second_pointer, &d.third_pointer})) return false;
    o.data = Boxed<Assoc3PointAngularDimActionBodyData>(std::move(d));
    return true;
}
void write_3point(RecordWriter& w, const Object& o) {
    const auto& d = data_of<Assoc3PointAngularDimActionBodyData>(o);
    w.object_head("ACDBASSOC3POINTANGULARDIMACTIONBODY", o);
    write_dimension_body(w, d.body, k3Point, {&d.first_pointer, &d.second_pointer, &d.third_pointer});
    w.xdata(o.xdata);
}
void three_point_references(const Object& o, std::vector<TypedReference>& out) {
    const auto& d = data_of<Assoc3PointAngularDimActionBodyData>(o);
    dimension_references(d.body, {&d.first_pointer, &d.second_pointer, &d.third_pointer}, out);
}

bool read_ordinate(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocOrdinateDimActionBodyData d;
    if (!read_dimension_body(view, ctx, d.body, kOrdinateSubclass, {&d.first_pointer, &d.second_pointer})) return false;
    o.data = Boxed<AssocOrdinateDimActionBodyData>(std::move(d));
    return true;
}
void write_ordinate(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocOrdinateDimActionBodyData>(o);
    w.object_head("ACDBASSOCORDINATEDIMACTIONBODY", o);
    write_dimension_body(w, d.body, kOrdinateSubclass, {&d.first_pointer, &d.second_pointer});
    w.xdata(o.xdata);
}
void ordinate_references(const Object& o, std::vector<TypedReference>& out) {
    const auto& d = data_of<AssocOrdinateDimActionBodyData>(o);
    dimension_references(d.body, {&d.first_pointer, &d.second_pointer}, out);
}

bool read_diametric(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocDiametricDimActionBodyData d;
    if (!read_dimension_body(view, ctx, d.body, kDiametric, {&d.pointer})) return false;
    o.data = Boxed<AssocDiametricDimActionBodyData>(std::move(d));
    return true;
}
void write_diametric(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocDiametricDimActionBodyData>(o);
    w.object_head("ACDBASSOCDIAMETRICDIMACTIONBODY", o);
    write_dimension_body(w, d.body, kDiametric, {&d.pointer});
    w.xdata(o.xdata);
}
void diametric_references(const Object& o, std::vector<TypedReference>& out) {
    const auto& d = data_of<AssocDiametricDimActionBodyData>(o);
    dimension_references(d.body, {&d.pointer}, out);
}

bool read_radial(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocRadialDimActionBodyData d;
    if (!read_dimension_body(view, ctx, d.body, kRadial, {&d.pointer})) return false;
    o.data = Boxed<AssocRadialDimActionBodyData>(std::move(d));
    return true;
}
void write_radial(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocRadialDimActionBodyData>(o);
    w.object_head("ACDBASSOCRADIALDIMACTIONBODY", o);
    write_dimension_body(w, d.body, kRadial, {&d.pointer});
    w.xdata(o.xdata);
}
void radial_references(const Object& o, std::vector<TypedReference>& out) {
    const auto& d = data_of<AssocRadialDimActionBodyData>(o);
    dimension_references(d.body, {&d.pointer}, out);
}

bool read_mleader(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocMLeaderActionBodyData d;
    if (!read_dimension_body(view, ctx, d.body, kMLeader, {&d.pointer}, {&d.value_90_a, &d.value_90_b})) return false;
    o.data = Boxed<AssocMLeaderActionBodyData>(std::move(d));
    return true;
}
void write_mleader(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocMLeaderActionBodyData>(o);
    w.object_head("ACDBASSOCMLEADERACTIONBODY", o);
    write_dimension_body(w, d.body, kMLeader, {&d.pointer}, {d.value_90_a, d.value_90_b});
    w.xdata(o.xdata);
}
void mleader_references(const Object& o, std::vector<TypedReference>& out) {
    const auto& d = data_of<AssocMLeaderActionBodyData>(o);
    dimension_references(d.body, {&d.pointer}, out);
}

// ---------------------------------------------------------------------------
// The entity clone body: no AutoCAD file has one (the head and a version of its own)
// ---------------------------------------------------------------------------

bool read_entity_clone(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocEntityCloneActionBodyData d;
    if (!read_assoc_action_body_head(view, ctx, d.head)) return false;
    GroupCursor c(view, "AcDbAssocEntityCloneActionBody");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocEntityCloneActionBody subclass");
    c.require_integer(90, d.version);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocEntityCloneActionBodyData>(std::move(d));
    return true;
}
void write_entity_clone(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocEntityCloneActionBodyData>(o);
    w.object_head("ACDBASSOCENTITYCLONEACTIONBODY", o);
    write_assoc_action_body_head(w, d.head);
    w.subclass("AcDbAssocEntityCloneActionBody");
    w.integer(90, d.version);
    w.xdata(o.xdata);
}
void entity_clone_references(const Object& o, std::vector<TypedReference>& out) {
    assoc_body_references(data_of<AssocEntityCloneActionBodyData>(o).head, out);
}

// ---------------------------------------------------------------------------
// The arrays
// ---------------------------------------------------------------------------

constexpr int kMatrixFlag = 4;
constexpr int kSecondPointerFlag = 16;
constexpr size_t kItemMatrixSize = 16;
constexpr size_t kArrayMatrixSize = 16;

bool read_matrix(GroupCursor& c, size_t n, std::vector<double>& out) {
    out.clear();
    for (size_t i = 0; i < n && c.ok(); ++i) {
        double v = 0.0;
        c.require_real(40, v);
        out.push_back(v);
    }
    return c.ok();
}

bool read_array_item(const RecordView& view, TypedReadContext& ctx, size_t index, bool last, AssocArrayItem& item,
                     std::vector<double>& array_matrix) {
    GroupCursor c(view, "AcDbAssocArrayItem", index);
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has fewer AcDbAssocArrayItem subclasses than its count");
    item = AssocArrayItem();
    c.require_integer(90, item.version);
    c.require_integer(90, item.item_index);
    c.require_integer(90, item.row_index);
    c.require_integer(90, item.level_index);
    c.require_integer(90, item.flags);
    if (c.ok()) {
        if (item.flags & kMatrixFlag) read_matrix(c, kItemMatrixSize, item.matrix);
        else c.require_point3(11, item.position);
    }
    c.require_handle(330, item.entity);
    if (c.ok() && (item.flags & kSecondPointerFlag)) c.require_handle(330, item.modify_body);
    if (c.ok() && last) read_matrix(c, kArrayMatrixSize, array_matrix);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    return true;
}

bool read_array_part(const RecordView& view, TypedReadContext& ctx, AssocActionBodyHead& head, AssocArrayBody& a) {
    if (!read_assoc_action_body_head(view, ctx, head)) return false;
    GroupCursor c(view, "AcDbAssocArrayActionBody");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocArrayActionBody subclass");
    a = AssocArrayBody();
    c.require_integer(90, a.version);
    std::string class_name;
    c.require_string(1, class_name);
    if (c.ok() && !assoc_array_kind_of(class_name, a.parameters.kind)) {
        c.fail("the array parameters class \"" + class_name + "\" is not established (AutoCAD's files have AcDbAssocArrayRectangularParameters, "
               "...PolarParameters, ...PathParameters and ...ModifyParameters)");
    }
    c.require_integer(90, a.parameters.version);
    int32_t n = 0;
    c.require_integer(90, n);
    c.require_string(1, a.item_class);
    if (!c.ok()) return ctx.fail(c.error());
    if (n < 0) return ctx.fail(assoc_describe(view) + ": a negative number of array items");
    // The items are subclasses of their own: the count is checked against the ones the record has before anything is allocated
    for (int32_t i = 0; i < n; ++i) {
        if (!GroupCursor(view, "AcDbAssocArrayItem", static_cast<size_t>(i)).valid()) {
            return ctx.fail(assoc_describe(view) + ": " + std::to_string(n) + " array items but fewer AcDbAssocArrayItem subclasses");
        }
    }
    a.items.reserve(static_cast<size_t>(n));
    for (int32_t i = 0; i < n; ++i) {
        AssocArrayItem item;
        if (!read_array_item(view, ctx, static_cast<size_t>(i), i + 1 == n, item, a.matrix)) return false;
        a.items.push_back(std::move(item));
    }
    if (n == 0) {
        read_matrix(c, kArrayMatrixSize, a.matrix);
        if (!c.ok()) return ctx.fail(c.error());
    }
    assoc_note_rest(c, ctx);
    return true;
}

bool write_array_part(RecordWriter& w, const AssocActionBodyHead& head, const AssocArrayBody& a) {
    write_assoc_action_body_head(w, head);
    for (const AssocArrayItem& item : a.items) {
        if ((item.flags & kMatrixFlag) && item.matrix.size() != kItemMatrixSize) {
            w.fail("an array item with flag 4 needs a matrix of 16 values");
            return false;
        }
    }
    if (a.matrix.size() != kArrayMatrixSize) {
        w.fail("an array body needs the array's matrix of 16 values");
        return false;
    }
    w.subclass("AcDbAssocArrayActionBody");
    w.integer(90, a.version);
    w.text(1, assoc_array_parameters_class(a.parameters.kind));
    w.integer(90, a.parameters.version);
    w.integer(90, static_cast<int64_t>(a.items.size()));
    w.text(1, a.item_class);
    for (size_t i = 0; i < a.items.size(); ++i) {
        const AssocArrayItem& item = a.items[i];
        w.subclass("AcDbAssocArrayItem");
        w.integer(90, item.version);
        w.integer(90, item.item_index);
        w.integer(90, item.row_index);
        w.integer(90, item.level_index);
        w.integer(90, item.flags);
        if (item.flags & kMatrixFlag) {
            for (double v : item.matrix) w.real(40, v);
        } else {
            w.point3(11, item.position);
        }
        w.handle(330, assoc_pointer(w, item.entity));
        if (item.flags & kSecondPointerFlag) w.handle(330, assoc_pointer(w, item.modify_body));
    }
    for (double v : a.matrix) w.real(40, v);
    return true;
}

void array_references(const AssocActionBodyHead& head, const AssocArrayBody& a, std::vector<TypedReference>& out) {
    assoc_body_references(head, out);
    for (const AssocArrayItem& item : a.items) {
        assoc_add_reference(out, item.entity, RefKind::SoftPointer);
        if (item.flags & kSecondPointerFlag) assoc_add_reference(out, item.modify_body, RefKind::SoftPointer);
    }
}

bool read_array_body(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocArrayActionBodyData d;
    if (!read_array_part(view, ctx, d.head, d.array)) return false;
    o.data = Boxed<AssocArrayActionBodyData>(std::move(d));
    return true;
}
void write_array_body(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocArrayActionBodyData>(o);
    w.object_head("ACDBASSOCARRAYACTIONBODY", o);
    if (!write_array_part(w, d.head, d.array)) return;
    w.xdata(o.xdata);
}
void array_body_references(const Object& o, std::vector<TypedReference>& out) {
    const auto& d = data_of<AssocArrayActionBodyData>(o);
    array_references(d.head, d.array, out);
}

bool read_array_modify(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocArrayModifyActionBodyData d;
    if (!read_array_part(view, ctx, d.head, d.array)) return false;
    GroupCursor c(view, "AcDbAssocArrayModifyActionBody");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocArrayModifyActionBody subclass");
    c.require_integer(90, d.version);
    c.require_integer(90, d.value_90_a);
    c.require_integer(90, d.value_90_b);
    c.require_integer(90, d.value_90_c);
    c.require_integer(90, d.value_90_d);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocArrayModifyActionBodyData>(std::move(d));
    return true;
}
void write_array_modify(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocArrayModifyActionBodyData>(o);
    w.object_head("ACDBASSOCARRAYMODIFYACTIONBODY", o);
    if (!write_array_part(w, d.head, d.array)) return;
    w.subclass("AcDbAssocArrayModifyActionBody");
    w.integer(90, d.version);
    w.integer(90, d.value_90_a);
    w.integer(90, d.value_90_b);
    w.integer(90, d.value_90_c);
    w.integer(90, d.value_90_d);
    w.xdata(o.xdata);
}
void array_modify_references(const Object& o, std::vector<TypedReference>& out) {
    const auto& d = data_of<AssocArrayModifyActionBodyData>(o);
    array_references(d.head, d.array, out);
}

std::vector<RefDescriptor> array_descriptors() {
    std::vector<RefDescriptor> refs = assoc_body_descriptors();
    refs.push_back(assoc_ref("AcDbAssocArrayItem", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace));
    return refs;
}

std::vector<ListSpec> array_lists() {
    // The parameters a version 1 body owns, the array part (its count is the number of items) and every item are lists
    return {assoc_whole_subclass("AcDbAssocParamBasedActionBody"), assoc_whole_subclass("AcDbAssocArrayActionBody"),
            assoc_whole_subclass("AcDbAssocArrayItem")};
}

const ClassCompanion kArrayCompanions[] = {{"ACDBASSOCARRAYRECTANGULARPARAMETERS", "AcDbAssocArrayRectangularParameters"},
                                           {"ACDBASSOCARRAYPOLARPARAMETERS", "AcDbAssocArrayPolarParameters"},
                                           {"ACDBASSOCARRAYPATHPARAMETERS", "AcDbAssocArrayPathParameters"}};
const ClassCompanion kArrayModifyCompanions[] = {{"ACDBASSOCARRAYMODIFYPARAMETERS", "AcDbAssocArrayModifyParameters"}};

void add_body(TypedRegistry& registry, const char* name, ObjectType type, ObjectReadFn read, ObjectWriteFn write,
              ObjectReferencesFn references, const char* cpp_name, std::vector<RefDescriptor> refs, std::vector<ListSpec> lists,
              bool verified, const ClassCompanion* companions = nullptr, size_t companion_count = 0) {
    ObjectCodec c;
    c.dxf_name = name;
    c.type = type;
    c.read = read;
    c.write = write;
    c.references = references;
    c.cls = assoc_class(cpp_name);
    c.cls.companions = companions;
    c.cls.companion_count = companion_count;
    c.versions = verified ? assoc_matrix(assoc_all_versions()) : assoc_matrix({});
    c.refs = std::move(refs);
    c.lists = std::move(lists);
    c.step = 15;
    registry.add(std::move(c));
}

std::vector<ListSpec> body_lists() { return {assoc_whole_subclass("AcDbAssocParamBasedActionBody")}; }

} // namespace

void register_assoc_dimension_array_codecs(TypedRegistry& registry) {
    add_body(registry, "ACDBASSOCROTATEDDIMACTIONBODY", ObjectType::AssocRotatedDimActionBody, read_rotated, write_rotated,
             rotated_references, "AcDbAssocRotatedDimActionBody", dimension_descriptors(kRotated), body_lists(), true);
    add_body(registry, "ACDBASSOC3POINTANGULARDIMACTIONBODY", ObjectType::Assoc3PointAngularDimActionBody, read_3point,
             write_3point, three_point_references, "AcDbAssoc3PointAngularDimActionBody", dimension_descriptors(k3Point), body_lists(), true);
    add_body(registry, "ACDBASSOCORDINATEDIMACTIONBODY", ObjectType::AssocOrdinateDimActionBody, read_ordinate, write_ordinate,
             ordinate_references, "AcDbAssocOrdinateDimActionBody", dimension_descriptors(kOrdinateSubclass), body_lists(), true);
    add_body(registry, "ACDBASSOCDIAMETRICDIMACTIONBODY", ObjectType::AssocDiametricDimActionBody, read_diametric, write_diametric,
             diametric_references, "AcDbAssocDiametricDimActionBody", dimension_descriptors(kDiametric), body_lists(), true);
    add_body(registry, "ACDBASSOCRADIALDIMACTIONBODY", ObjectType::AssocRadialDimActionBody, read_radial, write_radial,
             radial_references, "AcDbAssocRadialDimActionBody", dimension_descriptors(kRadial), body_lists(), true);
    add_body(registry, "ACDBASSOCMLEADERACTIONBODY", ObjectType::AssocMLeaderActionBody, read_mleader, write_mleader,
             mleader_references, "AcDbAssocMLeaderActionBody", dimension_descriptors(kMLeader), body_lists(), true);
    add_body(registry, "ACDBASSOCENTITYCLONEACTIONBODY", ObjectType::AssocEntityCloneActionBody, read_entity_clone,
             write_entity_clone, entity_clone_references, "AcDbAssocEntityCloneActionBody", assoc_body_descriptors(), body_lists(), false);
    add_body(registry, "ACDBASSOCARRAYACTIONBODY", ObjectType::AssocArrayActionBody, read_array_body, write_array_body,
             array_body_references, "AcDbAssocArrayActionBody", array_descriptors(), array_lists(), true, kArrayCompanions,
             sizeof(kArrayCompanions) / sizeof(kArrayCompanions[0]));
    add_body(registry, "ACDBASSOCARRAYMODIFYACTIONBODY", ObjectType::AssocArrayModifyActionBody, read_array_modify,
             write_array_modify, array_modify_references, "AcDbAssocArrayModifyActionBody", array_descriptors(), array_lists(), true,
             kArrayModifyCompanions, sizeof(kArrayModifyCompanions) / sizeof(kArrayModifyCompanions[0]));
}

} // namespace internal
} // namespace hdxf
