cmake_minimum_required(VERSION 3.20)

project(HDXF
    VERSION 1.9.1
    DESCRIPTION "C++ library for reading, writing and manipulating DXF files"
    LANGUAGES CXX
)

set(HDXF_VERSION_SUFFIX "")
if(HDXF_VERSION_SUFFIX)
    set(HDXF_VERSION_FULL "${PROJECT_VERSION}-${HDXF_VERSION_SUFFIX}")
else()
    set(HDXF_VERSION_FULL "${PROJECT_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Built on its own with a single-configuration generator and no build type: Release, so that the
# tests run optimized (nothing in the library or the tests depends on NDEBUG)
get_property(HDXF_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND NOT HDXF_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

# MSVC: the sources are UTF-8 (character literals such as '°'), and getenv() is the standard function the
# library uses for the font folders, not a deprecated one. Only HDxf's own targets: a directory property
if(MSVC)
    add_compile_options(/utf-8 /bigobj)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Library
add_subdirectory(src)

# The ACIS body builder (libs/hdxf_acis, target hdxf_acis, alias hdxf::acis): builds ACIS bodies from a
# program's exact B-rep. Needs hdxf only.
option(HDXF_BUILD_ACIS "Build the ACIS body builder (libs/hdxf_acis)" ON)
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(HDXF_ACIS_TESTS_DEFAULT ON)
else()
    set(HDXF_ACIS_TESTS_DEFAULT OFF)
endif()
option(HDXF_ACIS_BUILD_TESTS "Build the hdxf_acis tests" ${HDXF_ACIS_TESTS_DEFAULT})
if(HDXF_BUILD_ACIS)
    if(HDXF_ACIS_BUILD_TESTS)
        enable_testing()
    endif()
    add_subdirectory(libs/hdxf_acis)
endif()

# Install rules and the CMake package for find_package(hdxf): on when HDxf is built on its own, off when
# another project pulls it in, so that project's `cmake --install` does not take HDxf's files along.
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(HDXF_INSTALL_DEFAULT ON)
else()
    set(HDXF_INSTALL_DEFAULT OFF)
endif()
option(HDXF_INSTALL "Generate install rules and the hdxf CMake package" ${HDXF_INSTALL_DEFAULT})

# The installed package has the libraries and the public headers (include/hdxf.hpp, and hdxf_acis.hpp with
# the ACIS builder); the internal headers under src/ are not installed. The targets are hdxf::hdxf and
# hdxf::acis, the same names as the aliases in a source build.
if(HDXF_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(hdxf_install_targets hdxf)
    install(FILES include/hdxf.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    if(HDXF_BUILD_ACIS)
        set_target_properties(hdxf_acis PROPERTIES EXPORT_NAME acis)
        list(APPEND hdxf_install_targets hdxf_acis)
        install(FILES libs/hdxf_acis/include/hdxf_acis.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    endif()
    install(TARGETS ${hdxf_install_targets}
        EXPORT hdxfTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(EXPORT hdxfTargets
        NAMESPACE hdxf::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hdxf)
    configure_package_config_file(cmake/hdxfConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/hdxfConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hdxf)
    # Within one major version the API is compatible; a new major is never picked silently
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/hdxfConfigVersion.cmake
        COMPATIBILITY SameMajorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/hdxfConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/hdxfConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hdxf)
endif()

# Tests (only when building standalone, not as a dependency)
option(HDXF_BUILD_TESTS "Build HDXF tests" ON)
if(HDXF_BUILD_TESTS AND (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME))
    enable_testing()
    add_subdirectory(test)
endif()
