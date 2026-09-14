# Third-party dependency resolution.
#
# Heavy binary dependencies (Qt, Open CASCADE) come from find_package(); the
# deps/ superbuild produces them for toolchains without system packages.
# Light dependencies are pinned archives fetched with FetchContent, but an
# installed package is preferred when one is available (FIND_PACKAGE_ARGS).
include_guard(GLOBAL)

include(FetchContent)

# --- Catch2 (tests) ---------------------------------------------------------
if(BETTERCAD_BUILD_TESTS)
    set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(Catch2
        URL "https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.0.tar.gz"
        URL_HASH SHA256=0957cae5821b17ce07f0833aaa52b5137643a8382203221f363a8303c109af34
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SYSTEM
        FIND_PACKAGE_ARGS 3.16
    )
    # Catch2 does not support being built as a DLL; keep it static even when
    # BetterCAD itself is built with shared libraries.
    set(_bettercad_saved_shared_libs ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_MakeAvailable(Catch2)
    set(BUILD_SHARED_LIBS ${_bettercad_saved_shared_libs})
    if(DEFINED catch2_SOURCE_DIR)
        list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    elseif(DEFINED Catch2_DIR)
        list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")
    endif()
    set(BETTERCAD_CATCH2_VERSION "${Catch2_VERSION}")
    if(NOT BETTERCAD_CATCH2_VERSION)
        set(BETTERCAD_CATCH2_VERSION "3.16.0 (fetched)")
    endif()
endif()

# --- nlohmann/json (native file format; private to bettercad_io) -----------
# SYSTEM: third-party headers are exempt from BetterCAD's warning flags.
FetchContent_Declare(nlohmann_json
    URL "https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz"
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
    FIND_PACKAGE_ARGS 3.12
)
FetchContent_MakeAvailable(nlohmann_json)

# --- Eigen (sketch solver; private to bettercad_sketch) ----------------------
# Header-only. Eigen's own CMake project (tests, BLAS/LAPACK, docs) is not
# needed, so SOURCE_SUBDIR points at a directory without a CMakeLists.txt and
# the headers are exposed through a local interface target.
FetchContent_Declare(Eigen3
    URL "https://gitlab.com/libeigen/eigen/-/archive/5.0.1/eigen-5.0.1.tar.gz"
    URL_HASH SHA256=e9c326dc8c05cd1e044c71f30f1b2e34a6161a3b6ecf445d56b53ff1669e3dec
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR headers-only
)
FetchContent_MakeAvailable(Eigen3)
add_library(bettercad_eigen INTERFACE)
add_library(BetterCAD::eigen ALIAS bettercad_eigen)
target_include_directories(bettercad_eigen SYSTEM INTERFACE "${eigen3_SOURCE_DIR}")
set(BETTERCAD_EIGEN_VERSION "5.0.1")

# --- Open CASCADE Technology (geometry kernel; private to bettercad_geometry) -
# OCCT's package config appends Release-only compile definitions (UNICODE,
# NOMINMAX, _WIN32_WINNT, OCC_CONVERT_SIGNALS) to the calling directory. They
# are meant for OCCT's own build, so they are removed again to keep BetterCAD's
# Debug and Release builds identical. GLOBAL makes the imported TK* targets
# visible to every directory.
#
# No version is passed to find_package(): OCCT's version file only accepts an
# exact patch-level match, so the supported range is checked here instead.
get_directory_property(_bettercad_saved_definitions COMPILE_DEFINITIONS)
find_package(OpenCASCADE CONFIG QUIET GLOBAL)
set_directory_properties(PROPERTIES COMPILE_DEFINITIONS "${_bettercad_saved_definitions}")
if(NOT OpenCASCADE_FOUND)
    message(FATAL_ERROR
        "Open CASCADE Technology 8.0 was not found. Build it with the deps/ superbuild "
        "(see README.md) or point CMAKE_PREFIX_PATH / OpenCASCADE_DIR at an installation "
        "built with this compiler.")
endif()
if(OpenCASCADE_VERSION VERSION_LESS 8.0 OR OpenCASCADE_VERSION VERSION_GREATER_EQUAL 9.0)
    message(FATAL_ERROR
        "Open CASCADE Technology ${OpenCASCADE_VERSION} found in ${OpenCASCADE_DIR}; "
        "BetterCAD requires 8.0.x.")
endif()

# --- Qt 6 (desktop application) ---------------------------------------------
set(BETTERCAD_GUI_ENABLED OFF)
if(NOT BETTERCAD_BUILD_GUI STREQUAL "OFF")
    if(BETTERCAD_BUILD_GUI STREQUAL "ON")
        find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
    else()
        find_package(Qt6 6.5 QUIET COMPONENTS Widgets)
    endif()
    if(Qt6Widgets_FOUND)
        set(BETTERCAD_GUI_ENABLED ON)
    else()
        message(STATUS "Qt 6 not found: desktop application disabled (BETTERCAD_BUILD_GUI=AUTO)")
    endif()
endif()
