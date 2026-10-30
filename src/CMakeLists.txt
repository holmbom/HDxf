add_library(hdxf
    hdxf.cpp
    object_data.cpp
    drawing.cpp
    dxf_writer.cpp
    dxf_reader.cpp
    group_io.cpp
    raw_record.cpp
    object_reader.cpp
    dxf_writer_2000.cpp
    dxf_writer_acds.cpp
    acds.cpp
    dxf_writer_template.cpp
    template_2000.cpp
    extents.cpp
    acis.cpp
    acis_extents.cpp
    acis_sab.cpp
    drawing_r2000.cpp
    drawing_r13_r14.cpp
    drawing_r2007.cpp
    drawing_r2010.cpp
    drawing_r2013.cpp
    mleader.cpp
    drawing_mleader.cpp
    drawing_roundtrip.cpp
    dxf_writer_mleader.cpp
    dxf_writer_surface.cpp
    dxf_writer_mesh.cpp
    dxf_writer_geodata.cpp
    mesh.cpp
    mleader_graphics.cpp
    proxy_graphics.cpp
    text_encoding.cpp
    code_pages.cpp
    ecs.cpp
    aci_palette.cpp
    fonts.cpp
    text_layout.cpp
    mtext_layout.cpp
    mtext_data.cpp
    drawing_mtext.cpp
    drawing_attribute.cpp
    drawing_object_fields.cpp
    attribute_mirror.cpp
    dimension_block.cpp
    dimstyle_legacy.cpp
    dxf_writer_dimstyle.cpp
    dxf_writer_older_tables.cpp
    older_checks.cpp
    older_tables.cpp
    classes_older.cpp

    # Phase 9 (plan 2026-10-27-fas-9-typer.md): the foundation (9.0), then one block per family step. A family adds
    # its source files below its own marker and touches no other block.
    typed_record.cpp
    typed_relations.cpp
    typed_versions.cpp
    typed_families.cpp

    # 9.1 tables
    table_value.cpp
    table_format.cpp
    table_content.cpp
    table_geometry.cpp
    table_sync.cpp
    dxf_writer_table.cpp

    # 9.2 existing types and fields
    records_existing.cpp
    dxf_writer_existing.cpp

    # 9.3 OLE and proxy classes
    ole.cpp
    proxy_record.cpp
    proxy_record_graphics.cpp
    dxf_writer_proxy.cpp

    # 9.4 draw order, clipping and indexes
    objects_order_index.cpp
    dxf_writer_order_index.cpp

    # 9.5 table layout
    table_layout.cpp
    table_graphics.cpp

    # 9.6 underlays, coordination model, xref panels
    underlay.cpp
    coordination_model.cpp

    # 9.7 materials and visual styles
    material.cpp
    visual_style.cpp
    objects_style_defaults.cpp
    dxf_writer_styles.cpp

    # 9.8 fields and document data
    field.cpp
    data_table.cpp
    objects_document_data.cpp
    dxf_writer_document_data.cpp

    # 9.9 evaluation graph and dynamic block bases
    evaluation_graph.cpp
    dynamic_block_base.cpp
    dxf_writer_graph_block.cpp

    # 9.10 associative bases and persistent subentities
    assoc_action.cpp
    assoc_dependency.cpp
    assoc_perssubent.cpp
    assoc_param.cpp
    assoc_geometry.cpp
    assoc_body.cpp

    # 9.11 render, lights and sun
    objects_render.cpp
    sun_study.cpp
    dxf_writer_render.cpp

    # 9.12 sections and ModelDoc styles
    section.cpp
    section_graphics.cpp
    modeldoc_style.cpp

    # 9.13 dynamic blocks
    dynamic_block_parameters.cpp
    dynamic_block_actions.cpp
    dynamic_block_constraints.cpp
    dxf_writer_dynamic_types.cpp

    # 9.14 solid history
    solid_history.cpp
    dxf_writer_history.cpp

    # 9.15 other associative classes
    assoc_constraints.cpp
    assoc_extended_params.cpp
    assoc_surface_ops.cpp
    assoc_dimensions_arrays.cpp
    assoc_removal.cpp
    assoc_history.cpp
    assoc_extended.cpp

    # 9.16 NURBSURFACE and isolines
    acis_isolines.cpp
    surface_graphics.cpp
    nurbs_surface.cpp

    # 9.17 annotation contexts, DIMASSOC, special dimensions
    object_context.cpp
    dimassoc.cpp
    dimension_special.cpp
    dxf_writer_context_dimension.cpp

    # 9.18 Map, TC and ZWM
    map_objects.cpp
    tc_records.cpp
    zwm_records.cpp
    dxf_writer_extension_types.cpp
)

add_library(hdxf::hdxf ALIAS hdxf)

target_include_directories(hdxf
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(hdxf PUBLIC cxx_std_20)

# ObjectData's copy, move and destruction (include/hdxf.hpp) are optimized and without debug information in every
# build: otherwise libstdc++'s nested helpers for the some 200 alternatives are written out, each with a name of the
# whole list (a file of gigabytes)
set_source_files_properties(object_data.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-O2;-g0>")

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    target_compile_options(hdxf PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )
endif()
