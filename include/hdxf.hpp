#pragma once

// HDXF - DXF File Library, version 1.9.3
// Reads DXF R12 … 2018 (ASCII and binary); writes R12, R13, R14, 2000, 2004, 2007, 2010, 2013 and
// 2018. Every record of a file is preserved in full (RawRecord): a drawing read from a file is
// written back from them in its own version, with only what changed written anew.
//
// Every string in this API is UTF-8 (text, names, XDATA strings). Files are decoded
// from $DWGCODEPAGE (up to 2004) or UTF-8 (2007+). R12 is written in the drawing's
// $DWGCODEPAGE with \U+XXXX for characters the code page lacks; 2000 and 2004 have \U+XXXX
// for everything outside ASCII, 2007, 2010 and 2013 are UTF-8 — as AutoCAD writes them.

#include "hdxf/core.hpp"
#include "hdxf/boxed.hpp"
#include "hdxf/fwd.hpp"
#include "hdxf/proxy.hpp"
#include "hdxf/entity.hpp"
#include "hdxf/entity_viewport_image.hpp"
#include "hdxf/entity_surface.hpp"
#include "hdxf/entity_mleader.hpp"
#include "hdxf/entity_mesh.hpp"
#include "hdxf/entity_light_underlay.hpp"
#include "hdxf/table.hpp"
#include "hdxf/entity_special.hpp"
#include "hdxf/tables.hpp"
#include "hdxf/object_type.hpp"
#include "hdxf/eval_expr.hpp"
#include "hdxf/objects_base.hpp"
#include "hdxf/objects_assoc.hpp"
#include "hdxf/objects_viewstyle.hpp"
#include "hdxf/objects_map.hpp"
#include "hdxf/objects_tc.hpp"
#include "hdxf/objects_zwm.hpp"
#include "hdxf/objects_field.hpp"
#include "hdxf/objects_dynblock.hpp"
#include "hdxf/objects_material.hpp"
#include "hdxf/objects_history.hpp"
#include "hdxf/objects_context.hpp"
#include "hdxf/objects_render.hpp"
#include "hdxf/object.hpp"
#include "hdxf/drawing.hpp"
#include "hdxf/ecs.hpp"

namespace hdxf {

// ---------------------------------------------------------------------------
// Version
// ---------------------------------------------------------------------------

struct Version {
    static constexpr int major = 1;
    static constexpr int minor = 9;
    static constexpr int patch = 3;
    static constexpr const char* suffix = "";

    static std::string string() {
        std::string v = std::to_string(major) + "." +
                        std::to_string(minor) + "." +
                        std::to_string(patch);
        if (suffix[0] != '\0') v += "-" + std::string(suffix);
        return v;
    }
};

} // namespace hdxf
