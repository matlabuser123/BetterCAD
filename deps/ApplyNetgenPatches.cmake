# Apply BetterCAD's Netgen portability patches to an extracted source tree.
#
# Run as a script from the Netgen source directory:
#
#   cmake -DPATCH_DIR=<dir> -DGIT_EXECUTABLE=<git> -P ApplyNetgenPatches.cmake
#
# Every patch in PATCH_DIR is applied in sorted order. The step is idempotent:
# a patch that is already applied is skipped rather than failing, so a rebuild
# that re-runs ExternalProject's patch step does not break the source tree.
# A patch that neither applies nor is already applied is a hard error --
# silently continuing would build a Netgen that is not the one that was
# qualified.
#
# "git apply" is used purely as a patch tool; it does not require, and does not
# look for, a git repository here. core.autocrlf and core.eol are overridden on
# the command line because git still reads the *user's* global configuration
# when applying outside a repository: with autocrlf=true, a developer whose git
# is configured that way would get a source tree whose patched files have CRLF
# line endings while everyone else's have LF. The compiled result is the same,
# but the qualified dependency's source tree must not depend on a personal
# setting.

if(NOT PATCH_DIR)
    message(FATAL_ERROR "ApplyNetgenPatches: PATCH_DIR was not set")
endif()
if(NOT GIT_EXECUTABLE)
    find_package(Git REQUIRED)
endif()

file(GLOB patches "${PATCH_DIR}/*.patch")
list(SORT patches)
if(NOT patches)
    message(FATAL_ERROR "ApplyNetgenPatches: no patches found in ${PATCH_DIR}")
endif()

set(git_config_flags -c core.autocrlf=false -c core.eol=lf)
set(git_apply_flags -p1 --ignore-whitespace --whitespace=nowarn)

foreach(patch IN LISTS patches)
    get_filename_component(name "${patch}" NAME)

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" ${git_config_flags} apply ${git_apply_flags} --check "${patch}"
        RESULT_VARIABLE applies
        OUTPUT_QUIET ERROR_QUIET
    )
    if(applies EQUAL 0)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" ${git_config_flags} apply ${git_apply_flags} "${patch}"
            RESULT_VARIABLE applied
            ERROR_VARIABLE apply_error
        )
        if(NOT applied EQUAL 0)
            message(FATAL_ERROR "ApplyNetgenPatches: ${name} failed to apply: ${apply_error}")
        endif()
        message(STATUS "Netgen patch applied: ${name}")
        continue()
    endif()

    # Did not apply. Already applied, or genuinely wrong?
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" ${git_config_flags} apply ${git_apply_flags} --reverse --check "${patch}"
        RESULT_VARIABLE reverses
        OUTPUT_QUIET ERROR_QUIET
    )
    if(reverses EQUAL 0)
        message(STATUS "Netgen patch already applied, skipping: ${name}")
    else()
        message(FATAL_ERROR
            "ApplyNetgenPatches: ${name} neither applies to nor is already present in this "
            "source tree. The pinned Netgen version and the patch set have diverged; "
            "re-check deps/patches/netgen against BETTERCAD_DEPS_NETGEN_TAG.")
    endif()
endforeach()
