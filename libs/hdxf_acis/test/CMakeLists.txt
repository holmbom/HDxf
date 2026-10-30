# hdxf_acis tests. They use HDxf's test framework and, to look inside what was built, HDxf's internal
# ACIS reader (src/acis.hpp): the test target alone has those include paths; the library does not.
add_executable(hdxf_acis_tests
    main.cpp
    test_support.cpp
    test_model.cpp
    test_validation.cpp
    test_primitives.cpp
    test_spline.cpp
    test_encoding.cpp
    test_hdxf_roundtrip.cpp
    test_autocad_facit.cpp
)

target_link_libraries(hdxf_acis_tests PRIVATE hdxf::acis hdxf::hdxf)

target_include_directories(hdxf_acis_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/test
)

target_compile_definitions(hdxf_acis_tests PRIVATE
    HDXF_ACIS_FACIT_DIR="${PROJECT_SOURCE_DIR}/doc/facit"
    HDXF_ACIS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

add_test(NAME hdxf_acis_tests COMMAND hdxf_acis_tests)

# Tools: inspect_sat prints a body's records and their relations; export_cases writes the cases
# for the measurements in AutoCAD's core (tools/acad_measure.py)
add_executable(hdxf_acis_inspect_sat tools/inspect_sat.cpp)
target_link_libraries(hdxf_acis_inspect_sat PRIVATE hdxf::hdxf)
target_include_directories(hdxf_acis_inspect_sat PRIVATE ${PROJECT_SOURCE_DIR}/src)

add_executable(hdxf_acis_export_cases tools/export_cases.cpp test_support.cpp)
target_link_libraries(hdxf_acis_export_cases PRIVATE hdxf::acis hdxf::hdxf)
target_include_directories(hdxf_acis_export_cases PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/test
)
target_compile_definitions(hdxf_acis_export_cases PRIVATE
    HDXF_ACIS_FACIT_DIR="${PROJECT_SOURCE_DIR}/doc/facit"
)
