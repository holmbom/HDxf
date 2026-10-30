# hdxf_acis: ACIS bodies built from a program's exact B-rep (SAT 21500 for 3DSOLID, REGION and BODY).
# Depends on hdxf only (its public header). Added by the root CMakeLists.txt when HDXF_BUILD_ACIS is ON.

add_library(hdxf_acis
    src/model.cpp
    src/geometry.cpp
    src/validation.cpp
    src/sat_writer.cpp
    src/sat_topology.cpp
    src/sat_geometry.cpp
    src/sat_spline.cpp
    src/sat_encoding.cpp
    src/hdxf_adapter.cpp
    src/primitives.cpp
)

add_library(hdxf::acis ALIAS hdxf_acis)

target_compile_features(hdxf_acis PUBLIC cxx_std_20)

target_include_directories(hdxf_acis
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(hdxf_acis PUBLIC hdxf::hdxf)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    target_compile_options(hdxf_acis PRIVATE
        $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )
endif()

if(HDXF_ACIS_BUILD_TESTS)
    add_subdirectory(test)
endif()
