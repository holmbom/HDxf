#include "TypedRecordTypes.hpp"

// The families of phase 9 register their codecs here, one function each (plan 2026-10-27-fas-9-typer.md,
// steps 9.1 … 9.18). A family step does exactly two things in this file: declares its function and calls it
// in `register_typed_families`. Each step has its own blocks below, with a blank line between them, so that
// the waves' patches merge without conflicts — never edit another step's lines.
//
//   void register_table_codecs(TypedRegistry& registry);          // in table_value.cpp (or the family's file)
//   ...
//   register_table_codecs(registry);
//
// A function registers every codec of its family with registry.add(ObjectCodec{...}) / registry.add(
// EntityCodec{...}), and the relations and lists of a type that has no codec yet with add_relations() /
// add_lists(). The integration contract is in the plan, step 9.0.

namespace hdxf {
namespace internal {

// --- 9.1 tables ---
void register_table_codecs(TypedRegistry& registry);          // table_*.cpp, dxf_writer_table.cpp

// --- 9.2 existing types and fields ---
void register_existing_codecs(TypedRegistry& registry);          // in records_existing.cpp

// --- 9.3 OLE and proxy classes ---
void register_ole_codecs(TypedRegistry& registry);          // ole.cpp
void register_proxy_codecs(TypedRegistry& registry);        // proxy_record.cpp

// --- 9.4 draw order, clipping and indexes ---

void register_order_index_codecs(TypedRegistry& registry);   // in objects_order_index.cpp

// --- 9.5 table layout ---

// --- 9.6 underlays, coordination model, xref panels ---
void register_underlay_codecs(TypedRegistry& registry);       // underlay.cpp
void register_coordination_codecs(TypedRegistry& registry);   // coordination_model.cpp

// --- 9.7 materials and visual styles ---
void register_style_codecs(TypedRegistry& registry);         // material.cpp, visual_style.cpp, objects_style_defaults.cpp

// --- 9.8 fields and document data ---
void register_document_data_codecs(TypedRegistry& registry);   // field.cpp, data_table.cpp, objects_document_data.cpp

// --- 9.9 evaluation graph and dynamic block bases ---
void register_graph_block_codecs(TypedRegistry& registry);       // evaluation_graph.cpp, dynamic_block_base.cpp

// --- 9.10 associative bases and persistent subentities ---
void register_assoc_codecs(TypedRegistry& registry);         // assoc_*.cpp

// --- 9.11 render, lights and sun ---
void register_render_codecs(TypedRegistry& registry);         // objects_render.cpp
void register_sun_study_codecs(TypedRegistry& registry);      // sun_study.cpp (SUNSTUDY, SUN, LIGHT)

// --- 9.12 sections and ModelDoc styles ---
void register_section_codecs(TypedRegistry& registry);           // section.cpp, section_graphics.cpp
void register_modeldoc_style_codecs(TypedRegistry& registry);   // modeldoc_style.cpp

// --- 9.13 dynamic blocks ---
void register_dynamic_type_codecs(TypedRegistry& registry);   // dynamic_block_parameters.cpp, dynamic_block_actions.cpp, dynamic_block_constraints.cpp

// --- 9.14 solid history ---
void register_solid_history_codecs(TypedRegistry& registry);   // solid_history.cpp, dxf_writer_history.cpp

// --- 9.15 other associative classes ---
void register_assoc_extended_codecs(TypedRegistry& registry);   // assoc_constraints.cpp, assoc_extended_params.cpp, assoc_surface_ops.cpp, assoc_dimensions_arrays.cpp

// --- 9.16 NURBSURFACE and isolines ---
void register_nurbs_surface_codecs(TypedRegistry& registry);   // nurbs_surface.cpp (the codec since 9.S)

// --- 9.17 annotation contexts, DIMASSOC, special dimensions ---
void register_context_codecs(TypedRegistry& registry);            // object_context.cpp
void register_dimassoc_codecs(TypedRegistry& registry);           // dimassoc.cpp
void register_special_dimension_codecs(TypedRegistry& registry);  // dimension_special.cpp

// --- 9.18 Map, TC and ZWM ---
void register_map_codecs(TypedRegistry& registry);           // map_objects.cpp
void register_tc_codecs(TypedRegistry& registry);            // tc_records.cpp
void register_zwm_codecs(TypedRegistry& registry);           // zwm_records.cpp, dxf_writer_extension_types.cpp

// --- 9.S the proxies' class data (after every family: the classes' subclasses are the families') ---
void complete_proxy_codecs(TypedRegistry& registry);         // proxy_record.cpp

void register_typed_families(TypedRegistry& registry) {
    (void)registry;

    // --- 9.1 tables ---
    register_table_codecs(registry);

    // --- 9.2 existing types and fields ---
    register_existing_codecs(registry);

    // --- 9.3 OLE and proxy classes ---
    register_ole_codecs(registry);
    register_proxy_codecs(registry);

    // --- 9.4 draw order, clipping and indexes ---

    register_order_index_codecs(registry);

    // --- 9.5 table layout ---

    // --- 9.6 underlays, coordination model, xref panels ---
    register_underlay_codecs(registry);
    register_coordination_codecs(registry);

    // --- 9.7 materials and visual styles ---
    register_style_codecs(registry);

    // --- 9.8 fields and document data ---
    register_document_data_codecs(registry);

    // --- 9.9 evaluation graph and dynamic block bases ---
    register_graph_block_codecs(registry);

    // --- 9.10 associative bases and persistent subentities ---
    register_assoc_codecs(registry);

    // --- 9.11 render, lights and sun ---
    register_render_codecs(registry);
    register_sun_study_codecs(registry);

    // --- 9.12 sections and ModelDoc styles ---
    register_section_codecs(registry);
    register_modeldoc_style_codecs(registry);

    // --- 9.13 dynamic blocks ---
    register_dynamic_type_codecs(registry);

    // --- 9.14 solid history ---
    register_solid_history_codecs(registry);

    // --- 9.15 other associative classes ---
    register_assoc_extended_codecs(registry);

    // --- 9.16 NURBSURFACE and isolines ---
    register_nurbs_surface_codecs(registry);

    // --- 9.17 annotation contexts, DIMASSOC, special dimensions ---
    register_context_codecs(registry);
    register_dimassoc_codecs(registry);
    register_special_dimension_codecs(registry);

    // --- 9.18 Map, TC and ZWM ---
    register_map_codecs(registry);
    register_tc_codecs(registry);
    register_zwm_codecs(registry);

    // --- 9.S the proxies' class data ---
    complete_proxy_codecs(registry);
}

} // namespace internal
} // namespace hdxf
