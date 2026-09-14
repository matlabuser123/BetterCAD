# BetterCAD third-party dependency superbuild.
#
# Builds the heavy binary dependencies (Qt 6 qtbase, Open CASCADE Technology)
# from pinned, hash-verified source archives using the *same* toolchain as the
# main project, and installs them into a toolchain-keyed prefix that the main
# project searches automatically (see cmake/BetterCADDepsPrefix.cmake).
#
# Light header-only / test dependencies (Catch2, Eigen, nlohmann_json) are not
# built here; the main project obtains them with FetchContent.
#
#   cmake -S deps -B <deps-build-dir> -G Ninja
#   cmake --build <deps-build-dir>
#
# Keep <deps-build-dir> outside cloud-synced folders (OneDrive etc.): the Qt and
# OCCT build trees contain tens of thousands of files.
cmake_minimum_required(VERSION 3.25)

project(BetterCADDependencies LANGUAGES C CXX)

include(ExternalProject)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/BetterCADDepsPrefix.cmake")

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    bettercad_default_deps_prefix(default_prefix)
    set(CMAKE_INSTALL_PREFIX "${default_prefix}" CACHE PATH "Install prefix for BetterCAD dependencies" FORCE)
endif()

if(NOT CMAKE_BUILD_TYPE)
    # Release dependency binaries are usable from Debug and Release BetterCAD
    # builds on GCC/Clang. MSVC would need matching runtimes per configuration.
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Dependency build type" FORCE)
endif()

option(BETTERCAD_DEPS_QT "Build Qt 6 (qtbase) from source" ON)
option(BETTERCAD_DEPS_OCCT "Build Open CASCADE Technology from source" ON)

set(BETTERCAD_DEPS_QT_VERSION "6.11.2")
set(BETTERCAD_DEPS_QT_SHA256 "5b2e00eccaf5a4d8c14134ffa0ea8dfd0a35ae1ffc7f8d87fa4305a1ed23cf22")
set(BETTERCAD_DEPS_OCCT_TAG "V8_0_1")
set(BETTERCAD_DEPS_OCCT_SHA256 "0d6913eae4bcc09a3653ceced6dda1aec11c35a1513d4c06762c9b002092c68a")

set(common_cmake_args
    -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
    -DCMAKE_PREFIX_PATH:PATH=${CMAKE_INSTALL_PREFIX}
    -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}
)

# Every step uses the terminal (Ninja console pool), which serialises the
# projects while each one still builds with full internal parallelism.
set(terminal_steps
    USES_TERMINAL_DOWNLOAD TRUE
    USES_TERMINAL_CONFIGURE TRUE
    USES_TERMINAL_BUILD TRUE
    USES_TERMINAL_INSTALL TRUE
)

if(BETTERCAD_DEPS_QT)
    string(REGEX MATCH "^[0-9]+\\.[0-9]+" qt_minor "${BETTERCAD_DEPS_QT_VERSION}")
    ExternalProject_Add(qtbase
        URL "https://download.qt.io/official_releases/qt/${qt_minor}/${BETTERCAD_DEPS_QT_VERSION}/submodules/qtbase-everywhere-src-${BETTERCAD_DEPS_QT_VERSION}.tar.xz"
        URL_HASH SHA256=${BETTERCAD_DEPS_QT_SHA256}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/downloads"
        SOURCE_DIR "${CMAKE_BINARY_DIR}/src/qtbase"
        BINARY_DIR "${CMAKE_BINARY_DIR}/b/qtbase"
        CMAKE_ARGS
            ${common_cmake_args}
            -DBUILD_SHARED_LIBS:BOOL=ON
            -DQT_BUILD_EXAMPLES:BOOL=OFF
            -DQT_BUILD_TESTS:BOOL=OFF
            -DQT_BUILD_BENCHMARKS:BOOL=OFF
            -DQT_GENERATE_SBOM:BOOL=OFF
            -DFEATURE_sql:BOOL=OFF
            -DFEATURE_dbus:BOOL=OFF
            -DFEATURE_pkg_config:BOOL=OFF
            -DFEATURE_zstd:BOOL=OFF
            -DFEATURE_openssl:BOOL=OFF
            # Use Qt's bundled copies so the result does not depend on whatever
            # happens to be installed next to the compiler.
            -DFEATURE_system_zlib:BOOL=OFF
            -DFEATURE_system_pcre2:BOOL=OFF
            -DFEATURE_system_doubleconversion:BOOL=OFF
            -DFEATURE_system_freetype:BOOL=OFF
            -DFEATURE_system_harfbuzz:BOOL=OFF
            -DFEATURE_system_libpng:BOOL=OFF
            -DFEATURE_system_libjpeg:BOOL=OFF
            -DFEATURE_system_md4c:BOOL=OFF
        ${terminal_steps}
    )
endif()

if(BETTERCAD_DEPS_OCCT)
    set(occt_order)
    if(TARGET qtbase)
        # Not a real dependency: builds Qt first so the GUI is available early.
        set(occt_order DEPENDS qtbase)
    endif()
    ExternalProject_Add(occt
        URL "https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/${BETTERCAD_DEPS_OCCT_TAG}.tar.gz"
        URL_HASH SHA256=${BETTERCAD_DEPS_OCCT_SHA256}
        DOWNLOAD_NAME "occt-${BETTERCAD_DEPS_OCCT_TAG}.tar.gz"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/downloads"
        SOURCE_DIR "${CMAKE_BINARY_DIR}/src/occt"
        BINARY_DIR "${CMAKE_BINARY_DIR}/b/occt"
        ${occt_order}
        CMAKE_ARGS
            ${common_cmake_args}
            -DBUILD_LIBRARY_TYPE:STRING=Shared
            -DINSTALL_DIR_LAYOUT:STRING=Unix
            # Geometry kernel + STEP/STL exchange only. Required toolkits from
            # other modules are pulled in automatically by OCCT's CMake.
            -DBUILD_MODULE_FoundationClasses:BOOL=ON
            -DBUILD_MODULE_ModelingData:BOOL=ON
            -DBUILD_MODULE_ModelingAlgorithms:BOOL=ON
            -DBUILD_MODULE_DataExchange:BOOL=ON
            -DBUILD_MODULE_Visualization:BOOL=OFF
            -DBUILD_MODULE_ApplicationFramework:BOOL=OFF
            -DBUILD_MODULE_Draw:BOOL=OFF
            -DBUILD_DOC_Overview:BOOL=OFF
            -DBUILD_GTEST:BOOL=OFF
            # Keep Standard_Failure exceptions in release builds; BetterCAD
            # converts kernel failures into diagnostics instead of UB.
            -DBUILD_RELEASE_DISABLE_EXCEPTIONS:BOOL=OFF
            -DUSE_FREETYPE:BOOL=OFF
            -DUSE_TK:BOOL=OFF
            -DUSE_OPENGL:BOOL=OFF
            -DUSE_GLES2:BOOL=OFF
            -DUSE_D3D:BOOL=OFF
            -DUSE_FREEIMAGE:BOOL=OFF
            -DUSE_FFMPEG:BOOL=OFF
            -DUSE_OPENVR:BOOL=OFF
            -DUSE_RAPIDJSON:BOOL=OFF
            -DUSE_DRACO:BOOL=OFF
            -DUSE_TBB:BOOL=OFF
            -DUSE_VTK:BOOL=OFF
            -DUSE_EIGEN:BOOL=OFF
        ${terminal_steps}
    )
endif()

message(STATUS "BetterCAD dependencies will be installed to: ${CMAKE_INSTALL_PREFIX}")
