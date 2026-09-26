# Where BetterCAD is built, and the two ways that location can be wrong.
#
# BetterCAD builds into ${sourceDir}/build/<preset> by default. On a machine
# where the source tree sits in a synchronising folder (OneDrive, Dropbox), a
# file-replacing test can lose a race with the synchroniser and fail on a
# sharing violation, so the *-ext presets put the build tree somewhere else
# entirely, at BETTERCAD_BUILD_ROOT. Two failure modes come with that freedom,
# and both are silent enough to waste a day:
#
#   1. A preset that exists to leave the synchronised folder quietly builds
#      back inside it, because BETTERCAD_BUILD_ROOT was not set.
#   2. The chosen location collides with the dependency prefix in the 8.3
#      short-name namespace, and windeployqt deploys nothing usable
#      (INFRA-QT-DEPLOY-001).
#
# This module refuses both, at configure time, with a diagnostic that names the
# directories involved.
include_guard(GLOBAL)

# Where the batch helper lives, captured while this file is being read so that
# it is right no matter which directory a caller is in.
set(BETTERCAD_WINDOWS_SHORT_PATH_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/WindowsShortPath.cmd")

# The 8.3 short path of `path`, or "" when the filesystem has no short name for
# it (8.3 name creation disabled on the volume, or a non-Windows host).
#
# There is no CMake primitive for this, and `cmake -E` has no equivalent, so it
# goes through cmd.exe's `%~s` argument modifier, which only works on a batch
# parameter -- hence the helper script rather than an inline `cmd /c`. Passing
# the path as its own argument also keeps it away from cmd.exe's quoting rules,
# which quietly swallowed an inline `for` loop.
function(bettercad_short_path path out_var)
    set(${out_var} "" PARENT_SCOPE)
    if(NOT WIN32 OR NOT IS_DIRECTORY "${path}")
        return()
    endif()
    file(TO_NATIVE_PATH "${path}" native)
    execute_process(
        COMMAND cmd /c "${BETTERCAD_WINDOWS_SHORT_PATH_SCRIPT}" "${native}"
        OUTPUT_VARIABLE short
        ERROR_VARIABLE ignored
        RESULT_VARIABLE result
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT result EQUAL 0)
        return()
    endif()
    string(STRIP "${short}" short)
    file(TO_CMAKE_PATH "${short}" short)
    set(${out_var} "${short}" PARENT_SCOPE)
endfunction()

# TRUE when the filesystem has generated a short name for `path` itself -- that
# is, the last component of its short path is not simply its own name.
#
# Only the last component is compared: every ancestor contributes its own short
# name to the path, so a directory called "fits" under a long-named parent
# still has a short path full of tildes without having one of its own. Getting
# that wrong is the difference between checking something and checking nothing.
function(bettercad_has_generated_short_name path out_var)
    set(${out_var} FALSE PARENT_SCOPE)
    bettercad_short_path("${path}" short)
    if(short STREQUAL "")
        return()
    endif()
    get_filename_component(short_name "${short}" NAME)
    get_filename_component(long_name "${path}" NAME)
    string(TOLOWER "${short_name}" short_key)
    string(TOLOWER "${long_name}" long_key)
    if(NOT short_key STREQUAL long_key)
        set(${out_var} TRUE PARENT_SCOPE)
    endif()
endfunction()

# The first six characters an 8.3 short name would be generated from: the stem
# of the long name -- everything before the last dot, which becomes the
# extension -- with the characters that are illegal in 8.3 removed, and
# upper-cased.
#
# This only narrows the candidate list before the filesystem is asked, so
# over-selecting costs one subprocess. A name whose stem is shorter than six
# characters after the illegal characters are dropped simply yields a shorter
# basis, and two such names still compare equal to each other.
function(bettercad_short_name_basis name out_var)
    # Everything from the last dot is the extension, and plays no part in the
    # basis. A name with no dot is all stem.
    string(REGEX REPLACE "\\.[^.]*$" "" basis "${name}")
    # Illegal in an 8.3 name. Written as three replacements because CMake's
    # bracket expressions give a backslash no special meaning, so '[' and ']'
    # cannot be escaped inside one.
    string(REGEX REPLACE "[ +,;=.]" "" basis "${basis}")
    string(REPLACE "[" "" basis "${basis}")
    string(REPLACE "]" "" basis "${basis}")
    string(TOUPPER "${basis}" basis)
    string(LENGTH "${basis}" length)
    if(length GREATER 6)
        string(SUBSTRING "${basis}" 0 6 basis)
    endif()
    set(${out_var} "${basis}" PARENT_SCOPE)
endfunction()

# Find the sibling of `directory` that a lookup through its 8.3 short name
# would reach instead of it, or "" when that lookup reaches `directory` itself.
#
# NTFS generates a short name from the first six characters of the long name,
# with the characters illegal in 8.3 removed, plus "~N". Directories in one
# parent whose names reduce to the same six characters therefore all answer to
# variants of one short name, and a lookup that resolves it by ENUMERATING the
# parent reaches the first such directory in name order -- which is not
# necessarily the one the short name was taken from. That is the whole of
# INFRA-QT-DEPLOY-001: see docs/verification/INFRA-QT-DEPLOY-001/.
#
# The predicate is the measured one, and it is narrower than it first looked:
#
#   * The "~N" numbers do NOT decide it. A sibling that had its own distinct
#     number still took the lookup, so equal short names are not required.
#   * NAME ORDER decides it. With two same-basis siblings both sorting before
#     the dependency prefix, the FIRST of the two was reached.
#   * The six-character basis decides membership. A sibling sorting before the
#     prefix whose basis differed by one character was harmless.
function(bettercad_find_shadowing_sibling directory out_var)
    set(${out_var} "" PARENT_SCOPE)
    # A directory that needs no short name -- it already fits 8.3, or the
    # volume generates none -- is not reached through one.
    bettercad_has_generated_short_name("${directory}" generated)
    if(NOT generated)
        return()
    endif()

    get_filename_component(parent "${directory}" DIRECTORY)
    get_filename_component(name "${directory}" NAME)
    bettercad_short_name_basis("${name}" basis)
    if(basis STREQUAL "")
        return()
    endif()
    string(TOLOWER "${name}" name_key)

    # The first same-basis directory in the parent, in case-insensitive name
    # order, is the one the lookup reaches.
    set(first_key "${name_key}")
    set(first "")
    file(GLOB siblings LIST_DIRECTORIES true "${parent}/*")
    foreach(sibling IN LISTS siblings)
        if(NOT IS_DIRECTORY "${sibling}")
            continue()
        endif()
        get_filename_component(sibling_name "${sibling}" NAME)
        # Both names come from one directory listing, so comparing them needs
        # no path normalisation.
        if(sibling_name STREQUAL name)
            continue()
        endif()
        bettercad_short_name_basis("${sibling_name}" sibling_basis)
        if(NOT sibling_basis STREQUAL basis)
            continue()
        endif()
        string(TOLOWER "${sibling_name}" sibling_key)
        if(sibling_key STRLESS first_key)
            set(first_key "${sibling_key}")
            set(first "${sibling}")
        endif()
    endforeach()
    set(${out_var} "${first}" PARENT_SCOPE)
endfunction()

# Refuse a dependency prefix that a short-name lookup does not lead back to,
# because the Qt runtime deployed from it would be missing or wrong.
#
# Called only when the Qt application is actually being built: this reaches
# windeployqt, and nothing else BetterCAD does.
#
# A same-basis sibling that sorts AFTER the prefix is left alone. It was
# measured not to break deployment, and warning about every such directory
# would be noise on every configure; the day one appears that does break it,
# this fires with the name of it.
function(bettercad_check_deps_prefix_resolvable deps_prefix)
    if(NOT WIN32)
        return()
    endif()
    # The vulnerable component is the *root* of the prefix -- the directory
    # whose name is long enough to need a short name. The toolchain-keyed
    # subdirectory below it is resolved correctly either way.
    get_filename_component(deps_root "${deps_prefix}" DIRECTORY)
    bettercad_find_shadowing_sibling("${deps_root}" shadow)
    if(shadow STREQUAL "")
        return()
    endif()

    bettercad_short_path("${deps_root}" short)
    get_filename_component(deps_name "${deps_root}" NAME)
    get_filename_component(shadow_name "${shadow}" NAME)
    message(FATAL_ERROR
        "The dependency prefix is not what its own 8.3 short name leads to, so "
        "the Qt runtime cannot be deployed: '${shadow_name}' is reached "
        "instead of '${deps_name}'.
"
        "  dependencies:      ${deps_root}
"
        "  reached instead:   ${shadow}
"
        "  short name of the dependency prefix: ${short}
"
        "The first six characters of these two names are the same once the "
        "characters illegal in an 8.3 name are removed, so both answer to that "
        "short name, and a lookup that enumerates the parent takes the first "
        "of them in name order. windeployqt resolves the Qt binary directory "
        "that way, and then reports that it cannot find Qt6Core.dll under a "
        "path nobody configured.
"
        "Rename or remove '${shadow_name}', or point BETTERCAD_DEPS_PREFIX at "
        "a prefix whose name sorts before it."
    )
endfunction()

# Refuse to build back inside the source tree when the preset exists to get
# out of it. BETTERCAD_REQUIRE_EXTERNAL_BUILD_ROOT is set by the *-ext presets.
function(bettercad_check_build_root source_dir binary_dir)
    if(NOT BETTERCAD_REQUIRE_EXTERNAL_BUILD_ROOT)
        return()
    endif()
    if("$ENV{BETTERCAD_BUILD_ROOT}" STREQUAL "")
        message(FATAL_ERROR
            "This preset builds outside the source tree and needs "
            "BETTERCAD_BUILD_ROOT to say where.\n"
            "  configured binary directory: ${binary_dir}\n"
            "Set BETTERCAD_BUILD_ROOT to a directory on a local, "
            "non-synchronised volume and configure again."
        )
    endif()
    # An empty relative path means the two are the same directory, so this
    # covers equality as well as containment.
    file(RELATIVE_PATH relative "${source_dir}" "${binary_dir}")
    if(relative MATCHES "^\\.\\./" OR IS_ABSOLUTE "${relative}")
        set(inside FALSE)
    else()
        set(inside TRUE)
    endif()
    if(inside)
        message(FATAL_ERROR
            "This preset builds outside the source tree, but the build "
            "directory is inside it.\n"
            "  source directory: ${source_dir}\n"
            "  binary directory: ${binary_dir}\n"
            "  BETTERCAD_BUILD_ROOT: $ENV{BETTERCAD_BUILD_ROOT}\n"
            "Point BETTERCAD_BUILD_ROOT outside the source tree and "
            "configure again."
        )
    endif()
endfunction()
