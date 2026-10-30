#include "DynamicBlockElements.hpp"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// The parameters and grips of step 9.13 (phase 9; plan 2026-10-27-fas-9-typer.md, F9.9, B109) and what the step's three files share:
//
//   BLOCKALIGNMENTPARAMETER   AcDbEvalExpr, AcDbBlockElement, AcDbBlockParameter, AcDbBlock2PtParameter, AcDbBlockAlignmentParameter
//   BLOCKBASEPOINTPARAMETER   … AcDbBlock1PtParameter, AcDbBlockBasepointParameter
//   BLOCKFLIPPARAMETER        … 2Pt, AcDbBlockFlipParameter
//   BLOCKLOOKUPPARAMETER      … 1Pt, AcDbBlockLookUpParameter (capital U in the subclass, as in the files)
//   BLOCKPOINTPARAMETER       … 1Pt, AcDbBlockPointParameter
//   BLOCKPOLARPARAMETER       … 2Pt, AcDbBlockPolarParameter
//   BLOCKPROPERTIESTABLE      … 1Pt, AcDbBlockPropertiesTable
//   BLOCKROTATIONPARAMETER    … 2Pt, AcDbBlockRotationParameter
//   BLOCKUSERPARAMETER        … 1Pt, AcDbBlockUserParameter
//   BLOCKXYPARAMETER          … 2Pt, AcDbBlockXYParameter
//   the grips                 AcDbEvalExpr, AcDbBlockElement, AcDbBlockGrip and the own subclass (alignment and flip: 140 141 142 …;
//                             lookup, polar, rotation, XY: the marker only; the properties table's grip: none)
//
// The groups are what AutoCAD 2025's own dynamic block libraries have (the Sample/Dynamic Blocks DWGs saved as DXF 2000 … 2018,
// ASCII and binary: doc/facit/dyntypes, fas9_dynamic_types.scr); the records are the same in every version. 1010 … 1071 before the XDATA's
// 1001 are class data of their subclass, never XDATA (BlockGripData, Block1PtParameterData …). A record that does not follow the
// grammar (a missing group, a count above what remains, a variant tag no file shows) stays as read, with a warning (B105).

namespace hdxf {
namespace internal {

ObjectCodec dynamic_codec(const char* dxf_name, ObjectType type, ObjectReadFn read, ObjectWriteFn write, const char* cpp_name,
                          bool autocad_verified, int proxy_flags) {
    ObjectCodec c;
    c.dxf_name = dxf_name;
    c.type = type;
    c.read = read;
    c.write = write;
    c.cls = dynamic_block_class(cpp_name);
    c.cls.proxy_flags = proxy_flags;
    c.versions = dynamic_block_matrix();
    if (!autocad_verified) {
        // No AutoCAD file has a record of the type: the form is the one its siblings give, not verified
        for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013,
                             DxfVersion::R2018}) {
            c.versions.autocad(v, AutoCadStatus::NotTested);
        }
    }
    c.step = 13;
    return c;
}

std::vector<ListSpec> two_point_lists() {
    ListSpec s;
    s.subclass = "AcDbBlock2PtParameter";
    s.id = 1;
    s.shape = ListShape::Codes;
    s.codes = {170, 91};
    return {s};
}

ListSpec codes_list_of(const char* subclass, std::vector<int> codes, int id) {
    ListSpec s;
    s.subclass = subclass;
    s.id = id;
    s.shape = ListShape::Codes;
    s.codes = std::move(codes);
    return s;
}

ListSpec whole_subclass_list(const char* subclass) {
    ListSpec s;
    s.subclass = subclass;
    s.id = 1;
    s.shape = ListShape::WholeSubclass;
    return s;
}

RefDescriptor zero_pointer(const char* subclass, int code) {
    RefDescriptor d;
    d.subclass = subclass;
    d.code = code;
    d.kind = RefKind::SoftPointer;
    d.removal = RefRemoval::ZeroInPlace;
    return d;
}

std::vector<ListSpec> block_action_lists() {
    return {codes_list_of("AcDbBlockAction", {71, 330}, 1), codes_list_of("AcDbBlockAction", {70, 91}, 2)};
}

// ---------------------------------------------------------------------------
// The shared parts
// ---------------------------------------------------------------------------

void read_value_set(GroupCursor& c, int flags_code, int first_limit_code, int count_code, int value_code, BlockValueSet& set) {
    c.require_integer(flags_code, set.flags);
    for (int k = 0; k < 3; ++k) c.require_real(first_limit_code + k, set.limits[static_cast<size_t>(k)]);
    size_t n = 0;
    if (!c.ok()) return;
    if (!c.count(count_code, value_code ? 1 : 0, n)) {
        if (c.ok()) c.fail("group " + std::to_string(count_code) + " is missing");
        return;
    }
    if (n > 0 && value_code == 0) {
        c.fail("the value set has " + std::to_string(n) + " values: no AutoCAD file shows the group of a value of this set");
        return;
    }
    for (size_t i = 0; i < n && c.ok(); ++i) {
        double value = 0.0;
        c.require_real(value_code, value);
        set.values.push_back(value);
    }
}

void read_distance_part(GroupCursor& c, BlockDistancePart& d) {
    c.require_string(305, d.label);
    c.require_string(306, d.description);
    c.require_real(140, d.label_offset);
    c.require_string(307, d.expression);
    read_value_set(c, 96, 141, 175, 144, d.value_set);
}

bool read_frame_points(GroupCursor& c, int count_code, int point_code, std::vector<Point2>& points) {
    size_t n = 0;
    if (!c.count(count_code, 2, n)) {
        if (c.ok()) c.fail("group " + std::to_string(count_code) + " is missing");
        return false;
    }
    for (size_t i = 0; i < n && c.ok(); ++i) {
        Point2 p;
        if (!c.point2(point_code, p)) {
            if (c.ok()) c.fail("group " + std::to_string(point_code) + " is missing");
            return false;
        }
        points.push_back(p);
    }
    return c.ok();
}

void read_vector_140(GroupCursor& c, Point3& v) {
    c.require_real(140, v.x);
    c.require_real(141, v.y);
    c.require_real(142, v.z);
}

void read_links_interleaved(GroupCursor& c, int first_id_code, int first_name_code, BlockPropertyLink* links, size_t n) {
    for (size_t k = 0; k < n && c.ok(); ++k) {
        c.require_integer(first_id_code + static_cast<int>(k), links[k].node_id);
        c.require_string(first_name_code + static_cast<int>(k), links[k].name);
    }
}

void read_links_grouped(GroupCursor& c, int first_id_code, int first_name_code, BlockPropertyLink* links, size_t n) {
    for (size_t k = 0; k < n && c.ok(); ++k) c.require_integer(first_id_code + static_cast<int>(k), links[k].node_id);
    for (size_t k = 0; k < n && c.ok(); ++k) c.require_string(first_name_code + static_cast<int>(k), links[k].name);
}

// ---------------------------------------------------------------------------
// The own subclasses of the parameters and grips
// ---------------------------------------------------------------------------

void AlignmentParameterSpec::read_own(GroupCursor& c, Data& d) { c.require_integer(280, d.flag_280); }

void BasepointParameterSpec::read_own(GroupCursor& c, Data& d) {
    c.require_point3(1011, d.point_1011);
    c.require_point3(1012, d.point_1012);
}

void FlipParameterSpec::read_own(GroupCursor& c, Data& d) {
    c.require_string(305, d.label);
    c.require_string(306, d.description);
    c.require_string(307, d.text_307);
    c.require_string(308, d.text_308);
    c.require_point3(1012, d.point_1012);
    c.require_string(309, d.property_name);
    c.require_integer(96, d.flag_96);
}

void LookupParameterSpec::read_own(GroupCursor& c, Data& d) {
    c.require_string(303, d.label);
    c.require_string(304, d.description);
    c.require_integer(94, d.action_node_id);
}

void PointParameterSpec::read_own(GroupCursor& c, Data& d) {
    c.require_string(303, d.label);
    c.require_string(304, d.description);
    c.require_point3(1011, d.point_1011);
}

void PolarParameterSpec::read_own(GroupCursor& c, Data& d) {
    c.require_string(305, d.distance_label);
    c.require_string(306, d.distance_description);
    c.require_string(307, d.angle_label);
    c.require_string(308, d.angle_description);
    c.require_real(140, d.label_offset);
    c.require_string(309, d.text_309);
    if (!c.ok()) return;
    read_value_set(c, 96, 141, 175, 0, d.set_96);
    c.require_string(410, d.text_410);
    if (!c.ok()) return;
    read_value_set(c, 97, 145, 176, 0, d.set_97);
}

void RotationParameterSpec::read_own(GroupCursor& c, Data& d) {
    c.require_string(305, d.label);
    c.require_string(306, d.description);
    c.require_point3(1011, d.point_1011);
    c.require_real(140, d.label_offset);
    c.require_string(307, d.expression);
    if (!c.ok()) return;
    read_value_set(c, 96, 141, 175, 0, d.value_set);
}

void XYParameterSpec::read_own(GroupCursor& c, Data& d) {
    c.require_string(305, d.first_label);
    c.require_string(306, d.second_label);
    c.require_string(307, d.first_description);
    c.require_string(308, d.second_description);
    c.require_real(141, d.offset_141);
    c.require_real(140, d.offset_140);
    c.require_string(309, d.text_309);
    if (!c.ok()) return;
    read_value_set(c, 97, 146, 176, 0, d.set_97);
    c.require_string(410, d.text_410);
    if (!c.ok()) return;
    read_value_set(c, 96, 142, 175, 0, d.set_96);
}

void UserParameterSpec::read_own(GroupCursor& c, Data& d) {
    c.require_integer(90, d.value_90);
    c.require_handle(330, d.variable);
    c.require_string(301, d.name);
    c.require_string(309, d.text_309);
    c.require_integer(170, d.parameter_type);
}

void AlignmentGripSpec::read_own(GroupCursor& c, Data& d) { read_vector_140(c, d.vector); }

void FlipGripSpec::read_own(GroupCursor& c, Data& d) {
    read_vector_140(c, d.vector);
    c.require_integer(93, d.value_93);
}

void LookupGripSpec::read_own(GroupCursor&, Data&) {}
void PolarGripSpec::read_own(GroupCursor&, Data&) {}
void PropertiesTableGripSpec::read_own(GroupCursor&, Data&) {}
void RotationGripSpec::read_own(GroupCursor&, Data&) {}
void XYGripSpec::read_own(GroupCursor&, Data&) {}

// ---------------------------------------------------------------------------
// BLOCKPROPERTIESTABLE: the lookup table of a block table
//
//   AcDbBlockPropertiesTable: 90 (2), 300 name, 301 description, 91 columns, each column
//     340 (the parameter), 170, 171, 300, 301 (the property), 90 (the parameter's node id), 170 (-9999), a value (170 tag and its
//     300/140), 290 … 294, 302, 340;
//     92 rows, each row 90 (its number) and one value for each column; 93, 290, 291, 292.
//   A value is AcDbEvalVariant as the table writes it: 170 is the tag, 40 a real (140), 1 a text (300), -9999 none; any other
//   tag has no group in AutoCAD's files and fails the record.
// ---------------------------------------------------------------------------

namespace {

constexpr int16_t kTagNone = -9999;

bool read_table_value(GroupCursor& c, BlockTableValue& v) {
    if (!c.require_integer(170, v.tag)) return false;
    if (v.tag == 40) return c.require_real(140, v.real);
    if (v.tag == 1) return c.require_string(300, v.text);
    if (v.tag == kTagNone) return true;
    c.fail("a value of the properties table has the tag " + std::to_string(v.tag) + ": AutoCAD's files have 40, 1 and -9999");
    return false;
}

} // namespace

void PropertiesTableSpec::read_own(GroupCursor& c, Data& d) {
    c.require_integer(90, d.version);
    c.require_string(300, d.name);
    c.require_string(301, d.description);
    size_t columns = 0;
    if (!c.ok()) return;
    if (!c.count(91, 10, columns)) {
        if (c.ok()) c.fail("group 91 is missing");
        return;
    }
    for (size_t i = 0; i < columns && c.ok(); ++i) {
        BlockTableColumn col;
        c.require_handle(340, col.parameter);
        c.require_integer(170, col.flag_170);
        c.require_integer(171, col.flag_171);
        c.require_string(300, col.text_300);
        c.require_string(301, col.property_name);
        c.require_integer(90, col.node_id);
        c.require_integer(170, col.type_170);
        if (!c.ok()) return;
        read_table_value(c, col.value);
        c.require_boolean(290, col.flag_290);
        c.require_boolean(291, col.flag_291);
        c.require_boolean(292, col.flag_292);
        c.require_boolean(293, col.flag_293);
        c.require_boolean(294, col.flag_294);
        c.require_string(302, col.text_302);
        c.require_handle(340, col.pointer_340);
        if (c.ok()) d.columns.push_back(std::move(col));
    }
    size_t rows = 0;
    if (!c.ok()) return;
    // a row has its number and a value (two groups at least) for each column
    if (!c.count(92, 1 + columns, rows)) {
        if (c.ok()) c.fail("group 92 is missing");
        return;
    }
    for (size_t i = 0; i < rows && c.ok(); ++i) {
        BlockTableRow row;
        c.require_integer(90, row.index);
        for (size_t k = 0; k < columns && c.ok(); ++k) {
            BlockTableValue v;
            if (read_table_value(c, v)) row.values.push_back(std::move(v));
        }
        if (c.ok()) d.rows.push_back(std::move(row));
    }
    c.require_integer(93, d.value_93);
    c.require_boolean(290, d.flag_290);
    c.require_boolean(291, d.flag_291);
    c.require_boolean(292, d.flag_292);
}

// ---------------------------------------------------------------------------
// The codecs of the parameters and grips
// ---------------------------------------------------------------------------

namespace {

// What a parameter's own subclass has that the generic lists and relations do not cover
void table_references(const Object& o, std::vector<TypedReference>& out) {
    for (const BlockTableColumn& c : o.as_block_properties_table().columns) {
        if (!c.parameter.empty() && c.parameter != "0") out.push_back({c.parameter, RefKind::HardPointer, false});
    }
}

void user_references(const Object& o, std::vector<TypedReference>& out) {
    const std::string& h = o.as_block_user_parameter().variable;
    if (!h.empty() && h != "0") out.push_back({h, RefKind::SoftPointer, false});
}

RefDescriptor table_column_pointer() {
    RefDescriptor d;
    d.subclass = "AcDbBlockPropertiesTable";
    d.code = 340;
    d.kind = RefKind::HardPointer;
    d.removal = RefRemoval::ZeroInPlace;
    return d;
}

} // namespace

void register_dynamic_type_codecs(TypedRegistry& registry) {
    auto add_two_point = [&](ObjectCodec c) {
        c.lists = two_point_lists();
        registry.add(std::move(c));
    };
    auto two_point = [&](auto spec_tag, bool verified = true) {
        using Spec = decltype(spec_tag);
        return dynamic_codec(Spec::name, Spec::type, read_two_point<Spec>, write_two_point<Spec>, Spec::cpp_name, verified);
    };
    auto one_point = [&](auto spec_tag) {
        using Spec = decltype(spec_tag);
        return dynamic_codec(Spec::name, Spec::type, read_one_point<Spec>, write_one_point<Spec>, Spec::cpp_name);
    };
    auto grip = [&](auto spec_tag) {
        using Spec = decltype(spec_tag);
        return dynamic_codec(Spec::name, Spec::type, read_grip<Spec>, write_grip<Spec>, Spec::cpp_name);
    };

    // the 2Pt parameters
    add_two_point(two_point(AlignmentParameterSpec{}));
    add_two_point(two_point(FlipParameterSpec{}));
    add_two_point(two_point(PolarParameterSpec{}));
    add_two_point(two_point(RotationParameterSpec{}));
    add_two_point(two_point(XYParameterSpec{}));

    // the 1Pt parameters
    registry.add(one_point(BasepointParameterSpec{}));
    registry.add(one_point(LookupParameterSpec{}));
    registry.add(one_point(PointParameterSpec{}));
    {
        ObjectCodec c = one_point(PropertiesTableSpec{});
        c.references = table_references;
        c.lists = {whole_subclass_list("AcDbBlockPropertiesTable")};
        c.refs = {table_column_pointer()};
        registry.add(std::move(c));
    }
    {
        ObjectCodec c = one_point(UserParameterSpec{});
        c.references = user_references;
        c.refs = {zero_pointer("AcDbBlockUserParameter", 330)};
        registry.add(std::move(c));
    }

    // the grips
    registry.add(grip(AlignmentGripSpec{}));
    registry.add(grip(FlipGripSpec{}));
    registry.add(grip(LookupGripSpec{}));
    registry.add(grip(PolarGripSpec{}));
    registry.add(grip(PropertiesTableGripSpec{}));
    registry.add(grip(RotationGripSpec{}));
    registry.add(grip(XYGripSpec{}));

    register_dynamic_action_codecs(registry);
    register_dynamic_constraint_codecs(registry);
}

} // namespace internal
} // namespace hdxf
