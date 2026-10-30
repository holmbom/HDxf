#pragma once

#include "TypedRecordTypes.hpp"
#include "standard_objects.hpp"
#include <optional>
#include <string>
#include <vector>

// MATERIAL and VISUALSTYLE (phase 9, step 9.7): what material.cpp, visual_style.cpp, dxf_writer_styles.cpp and
// objects_style_defaults.cpp share. Internal.

namespace hdxf {
namespace internal {

// The codecs of both types (typed_families.cpp calls register_style_codecs, objects_style_defaults.cpp)
void register_style_codecs(TypedRegistry& registry);
void register_material_codec(TypedRegistry& registry);          // material.cpp
void register_visual_style_codec(TypedRegistry& registry);      // visual_style.cpp

// --- reading (material.cpp, visual_style.cpp) ---
// The typed values of one record's AcDbMaterial / AcDbVisualStyle subclass; `what` names the record in a failure
bool read_material_subclass(const RecordView& view, TypedReadContext& context, MaterialData& out, const std::string& what);
bool read_visual_style_subclass(const RecordView& view, TypedReadContext& context, VisualStyleData& out,
                                const std::string& what);

// --- writing (dxf_writer_styles.cpp) ---
void write_material(RecordWriter& w, const Object& o);
void write_visual_style(RecordWriter& w, const Object& o);

// --- the properties of a visual style (visual_style.cpp) ---
// The group code of a property's value in the two older forms, the extra true-color code of FaceMonoColor, EdgeColor and
// EdgeSilhouetteColor (421, 424, 425), 0 for a property without one
int visual_style_true_color_code(VisualStyleProperty property);
// Operations of a style: AutoCAD writes every property's operation; one that is not given is written as 1 (set)
constexpr int16_t kDefaultVisualStyleOperation = 1;

// A value of the property's kind, defaulted
VisualStyleValue default_visual_style_value(VisualStyleProperty property);

// Two materials with the same values, optional groups present in both or in neither (material.cpp)
bool material_equal(const MaterialData& a, const MaterialData& b);

// Two styles with the same form, name, type, internal flag, properties (values and operations) and trailing groups
bool visual_style_equal(const VisualStyleData& a, const VisualStyleData& b);

// The XRECORD of a style's extension dictionary (2007, 2010 forms; its first group is 102 RTVSPost2010Prop28): the
// properties 28 … 57 with their operations and, in the 2007 form, the operations of all properties. `decode` is false
// for an XRECORD that does not have that form (it is then left alone); `encode` makes its groups from the style
bool decode_visual_style_roundtrip(const std::vector<DxfGroup>& groups, VisualStyleData& style);
std::vector<DxfGroup> encode_visual_style_roundtrip(const VisualStyleData& style);
// The name of the first group of that XRECORD
extern const char* const kVisualStyleRoundTripFirst;

// --- AutoCAD's standard materials and visual styles as typed values (objects_style_defaults.cpp) ---
// The typed form of one of the records of the generated tables (drawing_r2007.cpp, drawing_r2010.cpp, drawing_r2013.cpp):
// a material; a visual style in the form its groups have, with the properties and operations of its XRECORD (`r.roundtrip`)
MaterialData typed_material(const DataRecord& record);
VisualStyleData typed_visual_style(const DataRecord& record);
// All of them as typed values: the three materials; the 26 styles of each of the three forms (0 Form2007, 1 Form2010, 2 Form2013)
const std::vector<MaterialData>& standard_typed_materials();
const std::vector<VisualStyleData>& standard_typed_styles(int form);

} // namespace internal
} // namespace hdxf
