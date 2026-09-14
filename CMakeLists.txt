cmake_minimum_required(VERSION 3.25)

project(BetterCAD
    VERSION 0.1.0
    DESCRIPTION "BetterCAD - parametric engineering CAD platform"
    LANGUAGES CXX
)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not supported. Use a separate build directory, "
        "for example: cmake --preset debug")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(BETTERCAD_BUILD_TESTS "Build the BetterCAD test suite" ${PROJECT_IS_TOP_LEVEL})
option(BETTERCAD_BUILD_CLI "Build the bettercad-cli command-line tool" ON)
option(BETTERCAD_BUILD_EXAMPLES "Build the example programs" ${PROJECT_IS_TOP_LEVEL})
set(BETTERCAD_BUILD_GUI "AUTO" CACHE STRING
    "Build the Qt desktop application: ON (require Qt 6), OFF, or AUTO (build if Qt 6 is found)")
set_property(CACHE BETTERCAD_BUILD_GUI PROPERTY STRINGS AUTO ON OFF)
option(BETTERCAD_WARNINGS_AS_ERRORS "Treat compiler warnings in BetterCAD code as errors" OFF)
option(BUILD_SHARED_LIBS "Build BetterCAD libraries as shared libraries" OFF)

include(BetterCADDepsPrefix)
bettercad_default_deps_prefix(_bettercad_default_deps_prefix)
set(BETTERCAD_DEPS_PREFIX "${_bettercad_default_deps_prefix}" CACHE PATH
    "Prefix containing dependencies built by the deps/ superbuild")
if(EXISTS "${BETTERCAD_DEPS_PREFIX}")
    list(PREPEND CMAKE_PREFIX_PATH "${BETTERCAD_DEPS_PREFIX}")
endif()

# ---------------------------------------------------------------------------
# Global build settings
# ---------------------------------------------------------------------------
get_property(_bettercad_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _bettercad_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build configuration" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# BetterCAD does not use C++ modules; skip the per-source dependency scan.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Keep all runtime artefacts together so executables find their DLLs.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")

set(BETTERCAD_GENERATED_INCLUDE_DIR "${PROJECT_BINARY_DIR}/generated/include")

include(GNUInstallDirs)
include(BetterCADCompilerOptions)
include(BetterCADTargets)
include(BetterCADDependencies)
include(BetterCADRuntime)

configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/templates/Version.hpp.in"
    "${BETTERCAD_GENERATED_INCLUDE_DIR}/bettercad/core/Version.hpp"
    @ONLY
)

# ---------------------------------------------------------------------------
# Components
# ---------------------------------------------------------------------------
add_subdirectory(src)
add_subdirectory(apps)
if(BETTERCAD_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BETTERCAD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(BetterCADSummary)
bettercad_print_summary()
