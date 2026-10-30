#pragma once

#include "TypedRecordTypes.hpp"
#include <string>
#include <vector>

// Step 9.11 of phase 9 (plan 2026-10-27-fas-9-typer.md): the render objects (RENDERSETTINGS, MENTALRAYRENDERSETTINGS,
// RAPIDRTRENDERSETTINGS, RENDERENVIRONMENT, RENDERGLOBAL, RAPIDRTRENDERENVIRONMENT, LIGHTLIST), SUNSTUDY and the two types
// that were typed before phase 9, LIGHT and SUN. Reading is in objects_render.cpp and sun_study.cpp, writing in
// dxf_writer_render.cpp; this header is what the three share.

namespace hdxf {
namespace internal {

// The writers: each writes the whole record (head, groups, XDATA)
void write_render_settings_record(RecordWriter& w, const Object& o);
void write_mentalray_settings_record(RecordWriter& w, const Object& o);
void write_rapidrt_settings_record(RecordWriter& w, const Object& o);
void write_render_environment_record(RecordWriter& w, const Object& o);
void write_render_global_record(RecordWriter& w, const Object& o);
void write_rapidrt_environment_record(RecordWriter& w, const Object& o);
void write_light_list_record(RecordWriter& w, const Object& o);
void write_sun_study_record(RecordWriter& w, const Object& o);
void write_sun_record(RecordWriter& w, const Object& o);
void write_light_record(RecordWriter& w, const Entity& e, const std::string& owner);

// The LIGHT's color as DXF 2000 has it: one group 90, an AcCmEntityColor (0xC2 and the true color's three bytes, or 0xC3
// and the color index), where 2004 and later have 63 with 421 (AutoCAD 2025's files: doc/facit/fas9_render.scr)
int32_t light_color_to_raw(const Color& color);
bool light_color_from_raw(int32_t raw, Color& color);

// A pointer to a record: the handle while the record is there (or led nowhere already in the file it was read from),
// 0 once it is gone (B19). While the drawing as it was read is written for comparison the handle stays as it was read.
std::string existing_pointer(const RecordWriter& w, const std::string& handle);

// The codecs of the family (objects_render.cpp, sun_study.cpp)
void register_render_codecs(TypedRegistry& registry);
void register_sun_study_codecs(TypedRegistry& registry);

} // namespace internal
} // namespace hdxf
