add_library(hdxf
    # The data variants' copying, moving and destruction, and the ACI palette
    core/aci_palette.cpp
    core/entity_data.cpp
    core/hdxf.cpp
    core/object_data.cpp

    # Group codes, text encoding and the ACDSDATA section
    io/acds.cpp
    io/code_pages.cpp
    io/group_io.cpp
    io/raw_record.cpp
    io/text_encoding.cpp

    # Reader
    read/dxf_reader.cpp
    read/object_reader.cpp

    # Writer
    write/classes_older.cpp
    write/dimstyle_legacy.cpp
    write/dxf_writer.cpp
    write/dxf_writer_2000.cpp
    write/dxf_writer_acds.cpp
    write/dxf_writer_context_dimension.cpp
    write/dxf_writer_dimstyle.cpp
    write/dxf_writer_document_data.cpp
    write/dxf_writer_dynamic_types.cpp
    write/dxf_writer_existing.cpp
    write/dxf_writer_extension_types.cpp
    write/dxf_writer_geodata.cpp
    write/dxf_writer_graph_block.cpp
    write/dxf_writer_history.cpp
    write/dxf_writer_mesh.cpp
    write/dxf_writer_mleader.cpp
    write/dxf_writer_older_tables.cpp
    write/dxf_writer_order_index.cpp
    write/dxf_writer_proxy.cpp
    write/dxf_writer_render.cpp
    write/dxf_writer_styles.cpp
    write/dxf_writer_surface.cpp
    write/dxf_writer_table.cpp
    write/dxf_writer_template.cpp
    write/older_tables.cpp
    write/template_2000.cpp

    # Drawing and its version specifics
    drawing/attribute_mirror.cpp
    drawing/drawing.cpp
    drawing/drawing_attribute.cpp
    drawing/drawing_mleader.cpp
    drawing/drawing_mtext.cpp
    drawing/drawing_object_fields.cpp
    drawing/drawing_polyline.cpp
    drawing/drawing_r13_r14.cpp
    drawing/drawing_r2000.cpp
    drawing/drawing_r2007.cpp
    drawing/drawing_r2010.cpp
    drawing/drawing_r2013.cpp
    drawing/drawing_roundtrip.cpp
    drawing/mleader.cpp
    drawing/older_checks.cpp
    drawing/table_sync.cpp

    # Record types: one codec per type in TypedRegistry
    records/assoc_action.cpp
    records/assoc_body.cpp
    records/assoc_constraints.cpp
    records/assoc_dependency.cpp
    records/assoc_dimensions_arrays.cpp
    records/assoc_extended.cpp
    records/assoc_extended_params.cpp
    records/assoc_geometry.cpp
    records/assoc_history.cpp
    records/assoc_param.cpp
    records/assoc_perssubent.cpp
    records/assoc_removal.cpp
    records/assoc_surface_ops.cpp
    records/coordination_model.cpp
    records/data_table.cpp
    records/dimassoc.cpp
    records/dimension_special.cpp
    records/dynamic_block_actions.cpp
    records/dynamic_block_base.cpp
    records/dynamic_block_constraints.cpp
    records/dynamic_block_parameters.cpp
    records/evaluation_graph.cpp
    records/field.cpp
    records/map_objects.cpp
    records/material.cpp
    records/modeldoc_style.cpp
    records/object_context.cpp
    records/objects_document_data.cpp
    records/objects_order_index.cpp
    records/objects_render.cpp
    records/objects_style_defaults.cpp
    records/ole.cpp
    records/proxy_record.cpp
    records/records_existing.cpp
    records/section.cpp
    records/solid_history.cpp
    records/sun_study.cpp
    records/table_content.cpp
    records/table_format.cpp
    records/table_geometry.cpp
    records/table_layout.cpp
    records/table_value.cpp
    records/tc_records.cpp
    records/typed_families.cpp
    records/typed_record.cpp
    records/typed_relations.cpp
    records/typed_versions.cpp
    records/underlay.cpp
    records/visual_style.cpp
    records/zwm_records.cpp

    # Extents, ACIS, mesh and surfaces
    geometry/acis.cpp
    geometry/acis_extents.cpp
    geometry/acis_isolines.cpp
    geometry/acis_sab.cpp
    geometry/ecs.cpp
    geometry/extents.cpp
    geometry/mesh.cpp
    geometry/nurbs_surface.cpp

    # Proxy graphics and dimension blocks
    graphics/dimension_block.cpp
    graphics/mleader_graphics.cpp
    graphics/proxy_graphics.cpp
    graphics/proxy_record_graphics.cpp
    graphics/section_graphics.cpp
    graphics/surface_graphics.cpp
    graphics/table_graphics.cpp

    # Fonts and text layout
    text/fonts.cpp
    text/mtext_data.cpp
    text/mtext_layout.cpp
    text/text_layout.cpp
)

add_library(hdxf::hdxf ALIAS hdxf)

target_include_directories(hdxf
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(hdxf PUBLIC cxx_std_20)

# The copying, moving and destruction of ObjectData and EntityData (include/hdxf/object.hpp, entity.hpp) are optimized and
# without debug information in every build: otherwise libstdc++'s nested helpers for each alternative are written out, each
# with a name of the whole list (a file of gigabytes)
set_source_files_properties(core/object_data.cpp core/entity_data.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-O2;-g0>")

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    target_compile_options(hdxf PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )
endif()
