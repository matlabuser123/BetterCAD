# Locate the Netgen volume-meshing backend built by the deps/ superbuild.
#
# Netgen does install a CMake package (NetgenConfig.cmake), but it is not
# usable here:
#
#   * its first line is set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"),
#     which changes the *consumer's* build settings as a side effect of being
#     found -- the same class of problem BetterCAD already neutralises around
#     find_package(OpenCASCADE);
#   * it bakes the build machine's absolute source directory into the
#     installed file (NETGEN_SOURCE_DIR), so the package is not relocatable
#     and leaks the environment it happened to be built in;
#   * it exports unnamespaced imported targets.
#
# Locating the two libraries directly is smaller, relocatable, and keeps the
# imported-target surface BetterCAD controls.
#
# Defines, on success:
#   Netgen::ngcore   the Netgen core runtime library
#   Netgen::nglib    the nglib meshing API; links ngcore and ZLIB transitively
#   Netgen_FOUND / Netgen_VERSION
#
# Only Netgen::nglib should be linked, and only from src/meshing/<backend>/:
# the architecture check (tests/architecture/CheckLayering.cmake, rule 5)
# confines mesh-backend headers to that directory.
include_guard(GLOBAL)

find_path(Netgen_INCLUDE_DIR
    NAMES nglib.h
    PATH_SUFFIXES include include/netgen
    DOC "Directory containing nglib.h"
)

find_library(Netgen_nglib_LIBRARY NAMES nglib DOC "Netgen nglib import library")
find_library(Netgen_ngcore_LIBRARY NAMES ngcore DOC "Netgen ngcore import library")

# The version is read from the header Netgen generates, not assumed: a
# dependency that misreports its own version is the defect patch 0004 fixes,
# and this is where that fix is observable from the outside.
set(Netgen_VERSION "")
if(Netgen_INCLUDE_DIR)
    # Netgen installs the generated version header one level down, in
    # include/include/, not beside nglib.h.
    foreach(_candidate
            "${Netgen_INCLUDE_DIR}/netgen_version.hpp"
            "${Netgen_INCLUDE_DIR}/include/netgen_version.hpp")
        if(NOT Netgen_VERSION AND EXISTS "${_candidate}")
            file(STRINGS "${_candidate}" _netgen_version_line
                 REGEX "^#define[ \t]+NETGEN_VERSION[ \t]+\"")
            if(_netgen_version_line MATCHES "\"([^\"]+)\"")
                set(Netgen_VERSION "${CMAKE_MATCH_1}")
            endif()
        endif()
    endforeach()
    unset(_candidate)
    unset(_netgen_version_line)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Netgen
    REQUIRED_VARS Netgen_nglib_LIBRARY Netgen_ngcore_LIBRARY Netgen_INCLUDE_DIR
    VERSION_VAR Netgen_VERSION
)

if(Netgen_FOUND AND NOT TARGET Netgen::nglib)
    # zlib is part of NETGEN's runtime closure, not a BetterCAD dependency:
    # libnglib.dll imports it, but BetterCAD never includes zlib.h or links it
    # directly. It is declared here, rather than through find_package(ZLIB),
    # because CMake's FindZLIB produces an UNKNOWN IMPORTED target whose
    # IMPORTED_LOCATION is the *import library*. $<TARGET_RUNTIME_DLLS>
    # ignores such a target, so the zlib DLL is never deployed, and the test
    # executable then links perfectly and fails to start.
    #
    # That failure is worth describing, because the error message does not
    # describe it: Windows reported a missing "api-ms-win-crt-time-l1-1-0.dll",
    # a UCRT apiset present on every Windows 10+ machine and entirely
    # innocent. A DLL-load error names an unreliable culprit.
    find_library(Netgen_zlib_LIBRARY NAMES zlib z zlib1 DOC "zlib import library used by Netgen")
    mark_as_advanced(Netgen_zlib_LIBRARY)
    if(NOT Netgen_zlib_LIBRARY)
        message(FATAL_ERROR
            "Netgen was found but zlib, which libnglib imports, was not. "
            "Build it with the deps/ superbuild (BETTERCAD_DEPS_ZLIB=ON).")
    endif()

    # Netgen installs nglib.h at include/ but its GENERATED headers
    # (netgen_version.hpp, netgen_config.hpp) one level down at
    # include/include/. Both directories are needed; Netgen's own config
    # package lists both for the same reason.
    set(Netgen_INCLUDE_DIRS "${Netgen_INCLUDE_DIR}")
    if(EXISTS "${Netgen_INCLUDE_DIR}/include")
        list(APPEND Netgen_INCLUDE_DIRS "${Netgen_INCLUDE_DIR}/include")
    endif()

    # On Windows find_library() returns the import library; the DLL sits in
    # bin/ beside it. IMPORTED_LOCATION must name the DLL so that
    # $<TARGET_RUNTIME_DLLS> can copy it (see cmake/BetterCADRuntime.cmake),
    # otherwise the build tree links but will not run.
    function(_bettercad_netgen_add_imported name implib)
        add_library(Netgen::${name} SHARED IMPORTED)
        # Include directories on an IMPORTED target are treated as SYSTEM by
        # default, which is what keeps BetterCAD's warning flags (and -Werror)
        # off third-party headers.
        set_target_properties(Netgen::${name} PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${Netgen_INCLUDE_DIRS}"
        )
        if(WIN32)
            # The DLL's name is DERIVED from the import library rather than
            # guessed: a MinGW import library is always <dll name>.a, so
            # libnglib.dll.a belongs to libnglib.dll. Guessing "lib${name}.dll"
            # would happen to work for these three and quietly mislead on the
            # first dependency that is named differently.
            get_filename_component(_lib_dir "${implib}" DIRECTORY)
            get_filename_component(_prefix "${_lib_dir}" DIRECTORY)
            get_filename_component(_implib_name "${implib}" NAME)
            if(NOT _implib_name MATCHES "\\.dll\\.a$")
                message(FATAL_ERROR
                    "Netgen: ${implib} is not a MinGW import library (*.dll.a), so the "
                    "matching DLL cannot be identified. Expected a shared build.")
            endif()
            string(REGEX REPLACE "\\.a$" "" _dll_name "${_implib_name}")
            set(_dll "${_prefix}/bin/${_dll_name}")
            if(NOT EXISTS "${_dll}")
                message(FATAL_ERROR
                    "Netgen import library ${implib} was found but the matching DLL is not at "
                    "${_dll}. The installation is incomplete; rebuild the deps/ superbuild.")
            endif()
            set_target_properties(Netgen::${name} PROPERTIES
                IMPORTED_LOCATION "${_dll}"
                IMPORTED_IMPLIB "${implib}"
            )
        else()
            set_target_properties(Netgen::${name} PROPERTIES IMPORTED_LOCATION "${implib}")
        endif()
    endfunction()

    _bettercad_netgen_add_imported(ngcore "${Netgen_ngcore_LIBRARY}")
    _bettercad_netgen_add_imported(zlib "${Netgen_zlib_LIBRARY}")
    _bettercad_netgen_add_imported(nglib "${Netgen_nglib_LIBRARY}")

    # nglib imports ngcore and zlib at load time. Declaring them here is what
    # lets $<TARGET_RUNTIME_DLLS> resolve the whole closure from one link, so
    # no caller has to know the backend has dependencies of its own.
    set_target_properties(Netgen::nglib PROPERTIES
        INTERFACE_LINK_LIBRARIES "Netgen::ngcore;Netgen::zlib"
    )

endif()

mark_as_advanced(Netgen_INCLUDE_DIR Netgen_nglib_LIBRARY Netgen_ngcore_LIBRARY)
