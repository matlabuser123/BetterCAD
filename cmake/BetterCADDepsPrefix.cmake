# Default location of third-party binaries produced by the deps/ superbuild.
#
# Shared by the superbuild (deps/CMakeLists.txt) and the main project so that
# both agree on where Qt and Open CASCADE live without per-machine settings.
# The prefix is keyed by toolchain: C++ binaries built by different compilers
# (or C runtimes) must never be mixed in one process.
include_guard(GLOBAL)

function(bettercad_deps_toolchain_key out_var)
    string(REGEX MATCH "^[0-9]+" compiler_major "${CMAKE_CXX_COMPILER_VERSION}")
    set(key "${CMAKE_CXX_COMPILER_ID}-${compiler_major}")
    if(MINGW)
        string(APPEND key "-mingw")
    elseif(MSVC)
        string(APPEND key "-msvc")
    endif()
    string(APPEND key "-${CMAKE_SYSTEM_PROCESSOR}")
    string(TOLOWER "${key}" key)
    set(${out_var} "${key}" PARENT_SCOPE)
endfunction()

function(bettercad_default_deps_root out_var)
    if(WIN32 AND DEFINED ENV{LOCALAPPDATA})
        file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}" base)
    elseif(DEFINED ENV{XDG_DATA_HOME})
        set(base "$ENV{XDG_DATA_HOME}")
    else()
        set(base "$ENV{HOME}/.local/share")
    endif()
    set(${out_var} "${base}/bettercad-deps" PARENT_SCOPE)
endfunction()

function(bettercad_default_deps_prefix out_var)
    bettercad_default_deps_root(root)
    bettercad_deps_toolchain_key(key)
    set(${out_var} "${root}/${key}" PARENT_SCOPE)
endfunction()
