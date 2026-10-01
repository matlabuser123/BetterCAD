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
option(BETTERCAD_DEPS_NETGEN "Build Netgen (volume meshing backend) from source" ON)
# zlib exists here only because Netgen's gzstream needs it. It is not a
# BetterCAD dependency in its own right and nothing in BetterCAD includes it.
option(BETTERCAD_DEPS_ZLIB "Build zlib from source (required by Netgen)" ${BETTERCAD_DEPS_NETGEN})

set(BETTERCAD_DEPS_QT_VERSION "6.11.2")
set(BETTERCAD_DEPS_QT_SHA256 "5b2e00eccaf5a4d8c14134ffa0ea8dfd0a35ae1ffc7f8d87fa4305a1ed23cf22")
set(BETTERCAD_DEPS_OCCT_TAG "V8_0_1")
set(BETTERCAD_DEPS_OCCT_SHA256 "0d6913eae4bcc09a3653ceced6dda1aec11c35a1513d4c06762c9b002092c68a")
set(BETTERCAD_DEPS_ZLIB_VERSION "1.3.1")
set(BETTERCAD_DEPS_ZLIB_SHA256 "9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23")
set(BETTERCAD_DEPS_NETGEN_TAG "v6.2.2604")
set(BETTERCAD_DEPS_NETGEN_SHA256 "0a614193ee6106c0c276a1516639bef872f2f2371dec159e1b1f6ffcb425954c")

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

if(BETTERCAD_DEPS_ZLIB)
    # Built from source with this toolchain on purpose. Netgen's own superbuild
    # downloads a PREBUILT zlib binary on Windows (cmake/external_projects/
    # zlib.cmake: no configure step, no build step, just a copy), which is
    # compiled against the MSVC runtime and imports VCRUNTIME140.dll. Mixing
    # that with a MinGW libstdc++ in one process is exactly the kind of
    # unverifiable foreign binary this project does not ship.
    ExternalProject_Add(zlib
        URL "https://github.com/madler/zlib/releases/download/v${BETTERCAD_DEPS_ZLIB_VERSION}/zlib-${BETTERCAD_DEPS_ZLIB_VERSION}.tar.gz"
        URL_HASH SHA256=${BETTERCAD_DEPS_ZLIB_SHA256}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/downloads"
        SOURCE_DIR "${CMAKE_BINARY_DIR}/src/zlib"
        BINARY_DIR "${CMAKE_BINARY_DIR}/b/zlib"
        CMAKE_ARGS ${common_cmake_args}
        ${terminal_steps}
    )
endif()

if(BETTERCAD_DEPS_NETGEN)
    # Netgen v6.2.2604 does not build with GCC/MinGW unmodified. The four
    # patches in patches/netgen/ are small, general and upstreamable; see
    # docs/verification/INFRA-NETGEN-001/PATCHES.md. git is used only as a
    # patch tool.
    find_package(Git REQUIRED)

    set(netgen_order)
    if(TARGET zlib)
        set(netgen_order DEPENDS zlib)
    endif()

    # Netgen derives its version with "git describe" and the archive is not a
    # git checkout, so without this it silently reports itself as 6.2.0 -- a
    # pinned dependency must not misname itself. TWEAK 0 means "a tagged
    # release", which yields exactly ${BETTERCAD_DEPS_NETGEN_TAG}.
    set(netgen_version_git "${BETTERCAD_DEPS_NETGEN_TAG}-0-0")

    ExternalProject_Add(netgen
        URL "https://github.com/NGSolve/netgen/archive/refs/tags/${BETTERCAD_DEPS_NETGEN_TAG}.tar.gz"
        URL_HASH SHA256=${BETTERCAD_DEPS_NETGEN_SHA256}
        DOWNLOAD_NAME "netgen-${BETTERCAD_DEPS_NETGEN_TAG}.tar.gz"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/downloads"
        SOURCE_DIR "${CMAKE_BINARY_DIR}/src/netgen"
        BINARY_DIR "${CMAKE_BINARY_DIR}/b/netgen"
        ${netgen_order}
        PATCH_COMMAND ${CMAKE_COMMAND}
            -DPATCH_DIR=${CMAKE_CURRENT_LIST_DIR}/patches/netgen
            -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
            -P ${CMAKE_CURRENT_LIST_DIR}/ApplyNetgenPatches.cmake
        CMAKE_ARGS
            ${common_cmake_args}
            # No superbuild: it downloads prebuilt Windows binaries (zlib, and
            # OCC/Tcl when those are enabled) from a third-party release page.
            # Everything BetterCAD links is built from pinned, hash-verified
            # source by this toolchain.
            -DUSE_SUPERBUILD:BOOL=OFF
            -DNETGEN_VERSION_GIT:STRING=${netgen_version_git}
            # -march=native would make meshing depend on the build machine's
            # instruction set, which the determinism contract forbids.
            -DUSE_NATIVE_ARCH:BOOL=OFF
            # Netgen's own OCC front end is off so there is exactly one path
            # from CAD to mesh, through the boundaries P16-GEOM-001 and
            # P16-SURF-001 already qualify (ADR-033).
            -DUSE_OCC:BOOL=OFF
            -DUSE_GUI:BOOL=OFF
            -DUSE_PYTHON:BOOL=OFF
            -DUSE_MPI:BOOL=OFF
            -DENABLE_UNIT_TESTS:BOOL=OFF
            # USE_STLGEOM / USE_CSG / USE_GEOM2D / USE_INTERFACE are left at
            # Netgen's defaults (ON) deliberately. Turning them off looks like
            # a smaller build but is not a configuration upstream supports:
            # nglib.cpp references netgen::mparam, which is defined in the
            # interface module, so USE_INTERFACE=OFF fails to link. BetterCAD
            # feeds nglib a surface mesh directly and never calls these
            # front ends; they cost binary size, not behaviour.
            #
            # makerls, Netgen's build-time mesh-rule generator, is linked
            # statically: it then cannot load a stray libstdc++-6.dll that
            # happens to sit earlier on PATH. Dynamically linked, it did, and
            # the resulting C++ ABI mismatch made it hang or crash. Only
            # executables are affected; nglib and ngcore stay shared.
            -DCMAKE_EXE_LINKER_FLAGS:STRING=-static
        ${terminal_steps}
    )
endif()

message(STATUS "BetterCAD dependencies will be installed to: ${CMAKE_INSTALL_PREFIX}")
