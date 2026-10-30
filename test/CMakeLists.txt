# External DXF test files (test/data/manifest.cmake) are downloaded at configure time,
# only when missing. Tests that need them are skipped if they are not there, so the
# build and the other tests work offline.
option(HDXF_TEST_DATA "Download external DXF test files" ON)
set(HDXF_EXTERNAL_DATA_DIR "${CMAKE_BINARY_DIR}/test-data")

function(hdxf_test_file name url sha256)
    set(target "${HDXF_EXTERNAL_DATA_DIR}/${name}")
    if(EXISTS "${target}")
        file(SHA256 "${target}" actual)
        if(actual STREQUAL sha256)
            return()
        endif()
        file(REMOVE "${target}")
    endif()

    set(partial "${target}.part")
    file(DOWNLOAD "${url}" "${partial}" STATUS status TIMEOUT 120 INACTIVITY_TIMEOUT 30)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        list(GET status 1 reason)
        file(REMOVE "${partial}")
        message(WARNING "HDxf test data: could not download ${name} (${reason}) - "
                        "tests that need it are skipped")
        return()
    endif()

    file(SHA256 "${partial}" actual)
    if(NOT actual STREQUAL sha256)
        file(REMOVE "${partial}")
        message(FATAL_ERROR "HDxf test data: SHA256 mismatch for ${name}\n"
                            "  expected ${sha256}\n  got      ${actual}\n"
                            "Configure with -DHDXF_TEST_DATA=OFF to skip the download.")
    endif()
    file(RENAME "${partial}" "${target}")
endfunction()

if(HDXF_TEST_DATA)
    include(${CMAKE_CURRENT_SOURCE_DIR}/data/manifest.cmake)
endif()

add_executable(hdxf_tests
    main.cpp
    test_drawing_builder.cpp
    test_basics.cpp
    test_entities.cpp
    test_drawing.cpp
    test_writer.cpp
    test_reader.cpp
    test_memory_api.cpp
    test_paths.cpp
    test_r12_fixes.cpp
    test_external_files.cpp
    test_synthetic_files.cpp
    test_group_io.cpp
    test_preserved_records.cpp
    test_acds.cpp
    test_handles.cpp
    test_read_2000.cpp
    test_text_encoding.cpp
    test_block_index.cpp
    test_versions.cpp
    test_write_r13_r14.cpp
    test_entities_r13_r14.cpp
    test_objects.cpp
    test_write_2000.cpp
    test_common_2000.cpp
    test_extents.cpp
    test_roundtrip_2000.cpp
    test_entities_2000.cpp
    test_text_2000.cpp
    test_curves_2000.cpp
    test_mline_image_2000.cpp
    test_write_2004.cpp
    test_color_2004.cpp
    test_transparency_2004.cpp
    test_fill_2004.cpp
    test_thumbnail.cpp
    test_code_pages.cpp
    test_fonts.cpp
    test_text_layout.cpp
    test_dimensions.cpp
    test_acis.cpp
    test_acis_sab.cpp
    test_acis_r13_r14.cpp
    test_write_2007.cpp
    test_write_2010.cpp
    test_write_2013.cpp
    test_write_2018.cpp
    test_mtext_2018.cpp
    test_attrib_2018.cpp
    test_objects_2018.cpp
    test_reference_2018.cpp
    test_regression_2018.cpp
    test_review_2018.cpp
    test_written_anew.cpp
    test_r2013_properties.cpp
    test_mesh.cpp
    test_geodata.cpp
    test_mleader.cpp
    test_mleader_graphics.cpp
    test_surface.cpp
    test_autocad_facit.cpp
    test_dimstyle_r13_r14.cpp
    test_tables_objects_r13_r14.cpp
    test_dimensions_r13_r14.cpp
    test_integration_r13_r14.cpp
    test_hatch_image_r13_r14.cpp
    test_roundtrip_r13_r14.cpp
    test_versions_r13_r14.cpp
    test_text_r13_r14.cpp
    test_coverage_r13_r14.cpp

    test_type_codecs.cpp
    test_all_typed_records.cpp
    # Phase 9: one block per family step; a family adds its test files below its own marker

    # 9.1 tables
    test_table_codecs.cpp
    test_table_relations.cpp

    # 9.2 existing types and fields
    test_existing_record_fields.cpp

    # 9.3 OLE and proxy classes
    test_ole_proxy.cpp
    test_proxy_extents.cpp

    # 9.4 draw order, clipping and indexes
    test_order_index.cpp

    # 9.5 table layout
    test_table_layout.cpp
    test_table_graphics.cpp

    # 9.6 underlays, coordination model, xref panels
    test_reference_types.cpp
    test_reference_graphics.cpp

    # 9.7 materials and visual styles
    test_material_visual_style.cpp

    # 9.8 fields and document data
    test_document_data.cpp
    test_field_relations.cpp

    # 9.9 evaluation graph and dynamic block bases
    test_graph_block_base.cpp
    test_dynamic_block_relations.cpp

    # 9.10 associative bases and persistent subentities
    test_assoc_build.cpp
    test_assoc_relations.cpp
    test_assoc_perssubent.cpp
    test_assoc_core.cpp

    # 9.11 render, lights and sun
    test_render_types.cpp
    test_render_relations.cpp

    # 9.12 sections and ModelDoc styles
    test_section_types.cpp
    test_modeldoc_styles.cpp

    # 9.13 dynamic blocks
    test_dynamic_types.cpp

    # 9.14 solid history
    test_solid_history.cpp

    # 9.15 other associative classes
    test_assoc_extended.cpp
    test_assoc_history.cpp
    test_assoc_removal.cpp
    test_assoc_extended_build.cpp

    # 9.16 NURBSURFACE and isolines
    test_surface_isolines.cpp
    test_nurbs_surface.cpp

    # 9.17 annotation contexts, DIMASSOC, special dimensions
    test_context_dimension.cpp
    test_context_relations.cpp

    # 9.18 Map, TC and ZWM
    test_map_types.cpp
    test_tc_zwm_types.cpp
)

target_link_libraries(hdxf_tests PRIVATE hdxf::hdxf)

# Line tables only in the tests: ObjectData's std::variant (include/hdxf.hpp, some 200 alternatives) gives each file
# that uses an Object 10-25 MB of type information, and with the library's the executable's image passes the 2 GB
# Windows loads ("Exec format error")
target_compile_options(hdxf_tests PRIVATE $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>>:-g1>)

target_include_directories(hdxf_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
)

target_compile_definitions(hdxf_tests PRIVATE
    HDXF_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    HDXF_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    HDXF_EXTERNAL_DATA_DIR="${HDXF_EXTERNAL_DATA_DIR}"
)

add_test(NAME hdxf_tests COMMAND hdxf_tests)

# Phase 9's tools (step 9.0): the inventory's self-test and the check that the type register in src/ and test/data/
# is what tools/gen_type_registry.py makes of the plans and the reader's dispatch
find_program(HDXF_PYTHON NAMES python3 python)
if(HDXF_PYTHON)
    add_test(NAME inventory_types_selftest
             COMMAND ${HDXF_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/inventory_types.py --self-test)
    add_test(NAME fas9_facit_run_selftest
             COMMAND ${HDXF_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/fas9_facit_run.py --self-test)
    add_test(NAME type_registry_is_generated
             COMMAND ${HDXF_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/gen_type_registry.py --check)
endif()

add_executable(generate_test_dxf
    generate_test_dxf.cpp
    test_drawing_builder.cpp
)
target_link_libraries(generate_test_dxf PRIVATE hdxf::hdxf)
target_include_directories(generate_test_dxf PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

# 9.S verification batch (tools/fas9_verify_batch.py): reads one DXF and writes it again in its own version, ASCII and
# binary, unchanged and with every typed record written anew from its typed values alone
add_executable(hdxf_roundtrip
    ${PROJECT_SOURCE_DIR}/tools/hdxf_roundtrip.cpp
)
target_link_libraries(hdxf_roundtrip PRIVATE hdxf::hdxf)
if(HDXF_PYTHON)
    add_test(NAME fas9_verify_batch_selftest
             COMMAND ${HDXF_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/fas9_verify_batch.py --self-test)
endif()
