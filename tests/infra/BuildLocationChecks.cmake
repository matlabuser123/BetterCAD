# Script mode (cmake -P): the build-location guards in
# cmake/BetterCADBuildLocation.cmake.
#
# The guards exist to turn two silent traps into configure-time errors: a
# preset that exists to leave a synchronised folder building back inside it,
# and a dependency prefix whose 8.3 short name is ambiguous
# (INFRA-QT-DEPLOY-001). A guard that cannot fire is worth nothing, so each
# one is made to fire here.
#
# Inputs: CASE, MODULE_DIR, WORK_DIR
foreach(required CASE MODULE_DIR WORK_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "BuildLocationChecks.cmake: ${required} is not set")
    endif()
endforeach()

list(APPEND CMAKE_MODULE_PATH "${MODULE_DIR}")
include(BetterCADBuildLocation)

function(expect_equal actual expected what)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "${what}: got '${actual}', expected '${expected}'")
    endif()
endfunction()

# CMake wraps a long message across lines at whitespace, so an expected phrase
# would otherwise have to know where the wrap falls. Collapsing runs of
# whitespace lets the expectations be written as the sentences they are.
function(flatten text out_var)
    string(REGEX REPLACE "[ \t\r\n]+" " " text "${text}")
    set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

# Run a guard in its own process, because a guard reports by failing, and
# assert it failed for the stated reason.
function(expect_guard_failure body expected_regex what)
    set(script "${WORK_DIR}/guard-${what}.cmake")
    file(WRITE "${script}"
        "list(APPEND CMAKE_MODULE_PATH \"${MODULE_DIR}\")\n"
        "include(BetterCADBuildLocation)\n"
        "${body}\n")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "--unset=BETTERCAD_BUILD_ROOT"
            "${CMAKE_COMMAND}" -P "${script}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
    )
    if(result EQUAL 0)
        message(FATAL_ERROR "${what}: the guard did not fire:\n${output}")
    endif()
    flatten("${output}" output)
    if(NOT output MATCHES "${expected_regex}")
        message(FATAL_ERROR
            "${what}: the guard fired but did not explain itself.\n"
            "  expected to match: ${expected_regex}\n"
            "  said:\n${output}")
    endif()
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

if(CASE STREQUAL "short-name-basis")
    # An 8.3 short name is generated from the first six characters of the long
    # name with the characters illegal in 8.3 removed. Two names with the same
    # basis compete for one short name; that is the whole precondition for the
    # collision, so the basis must be computed the same way for both.
    bettercad_short_name_basis("bettercad-deps" basis)
    expect_equal("${basis}" "BETTER" "bettercad-deps")
    bettercad_short_name_basis("BetterCAD-build" basis)
    expect_equal("${basis}" "BETTER" "BetterCAD-build has the same basis")
    bettercad_short_name_basis("bettercad-aaa" basis)
    expect_equal("${basis}" "BETTER" "bettercad-aaa has the same basis")
    bettercad_short_name_basis("build" basis)
    expect_equal("${basis}" "BUILD" "a name shorter than six characters")
    # The extension is everything after the last dot and plays no part in the
    # basis, and a space is illegal in an 8.3 name: "my dir.name" is generated
    # from "mydir".
    bettercad_short_name_basis("my dir.name" basis)
    expect_equal("${basis}" "MYDIR" "spaces and the extension are not in the basis")
    bettercad_short_name_basis("a+b,c;d=e" basis)
    expect_equal("${basis}" "ABCDE" "characters illegal in 8.3 are dropped")

elseif(CASE STREQUAL "shadowing-sibling-is-found")
    # The condition that broke deployment, reconstructed from directory names
    # alone: a sibling sharing the six-character 8.3 basis, sorting first.
    file(MAKE_DIRECTORY "${WORK_DIR}/one/bettercad-deps")
    file(MAKE_DIRECTORY "${WORK_DIR}/one/bettercad-aaa")
    bettercad_find_shadowing_sibling("${WORK_DIR}/one/bettercad-deps" shadow)
    expect_equal("${shadow}" "${WORK_DIR}/one/bettercad-aaa"
        "a same-basis sibling sorting before the prefix")

    # With two of them, the FIRST in name order is the one reached -- measured
    # against windeployqt, not assumed.
    file(MAKE_DIRECTORY "${WORK_DIR}/two/bettercad-deps")
    file(MAKE_DIRECTORY "${WORK_DIR}/two/bettercad-aaa")
    file(MAKE_DIRECTORY "${WORK_DIR}/two/BetterCAD-build")
    bettercad_find_shadowing_sibling("${WORK_DIR}/two/bettercad-deps" shadow)
    expect_equal("${shadow}" "${WORK_DIR}/two/bettercad-aaa"
        "the first of two same-basis siblings")

    # Comparison ignores case, or `BetterCAD-build` would not have been found
    # at all -- it was the directory that caused this milestone.
    file(MAKE_DIRECTORY "${WORK_DIR}/case/bettercad-deps")
    file(MAKE_DIRECTORY "${WORK_DIR}/case/BetterCAD-build")
    bettercad_find_shadowing_sibling("${WORK_DIR}/case/bettercad-deps" shadow)
    expect_equal("${shadow}" "${WORK_DIR}/case/BetterCAD-build"
        "a same-basis sibling differing in case")

elseif(CASE STREQUAL "no-shadowing-sibling")
    # The search globs a whole parent directory, so a false positive here would
    # fail every configure on the machine.
    file(MAKE_DIRECTORY "${WORK_DIR}/alone/bettercad-deps")
    bettercad_find_shadowing_sibling("${WORK_DIR}/alone/bettercad-deps" shadow)
    expect_equal("${shadow}" "" "a directory alone in its parent")

    # Sorting AFTER the prefix is harmless: the lookup reaches the prefix
    # first. Measured -- `bettercad-zzz` deployed correctly.
    file(MAKE_DIRECTORY "${WORK_DIR}/after/bettercad-deps")
    file(MAKE_DIRECTORY "${WORK_DIR}/after/bettercad-zzz")
    bettercad_find_shadowing_sibling("${WORK_DIR}/after/bettercad-deps" shadow)
    expect_equal("${shadow}" "" "a same-basis sibling sorting after the prefix")

    # A different basis is harmless even when it sorts first: `bette-aaa`
    # reduces to BETTE- , not BETTER, and deployed correctly.
    file(MAKE_DIRECTORY "${WORK_DIR}/basis/bettercad-deps")
    file(MAKE_DIRECTORY "${WORK_DIR}/basis/bette-aaa")
    file(MAKE_DIRECTORY "${WORK_DIR}/basis/build")
    file(MAKE_DIRECTORY "${WORK_DIR}/basis/something-else")
    bettercad_find_shadowing_sibling("${WORK_DIR}/basis/bettercad-deps" shadow)
    expect_equal("${shadow}" "" "siblings whose basis differs")

elseif(CASE STREQUAL "short-path-of-a-generated-name")
    # The positive case, and the one that matters: a name too long for 8.3 must
    # come back as a generated short name. Returning "" is how this helper says
    # "no short name here", so a helper that fails to run at all reports the
    # same thing as a filesystem with no short names -- and every check built on
    # it then passes while testing nothing. That happened. Hence the
    # independent probe below rather than trusting the helper's own answer.
    file(MAKE_DIRECTORY "${WORK_DIR}/a-long-directory-name")
    file(WRITE "${WORK_DIR}/a-long-directory-name/marker.txt" "marker")
    file(TO_NATIVE_PATH "${WORK_DIR}" native_work)
    execute_process(
        COMMAND cmd /c dir /x /ad "${native_work}"
        OUTPUT_VARIABLE listing
        ERROR_VARIABLE listing
        RESULT_VARIABLE listing_result
    )
    if(NOT listing_result EQUAL 0)
        message(FATAL_ERROR "cannot list ${WORK_DIR}:\n${listing}")
    endif()

    bettercad_short_path("${WORK_DIR}/a-long-directory-name" short)
    bettercad_has_generated_short_name("${WORK_DIR}/a-long-directory-name" generated)
    if(NOT listing MATCHES "~")
        # No entry in this directory has a generated short name, so the
        # filesystem creates none and there is nothing to collide.
        if(generated)
            message(FATAL_ERROR
                "the filesystem reports no short names, but a generated one "
                "was claimed anyway: ${short}")
        endif()
        message(STATUS "8.3 short names are not created on this filesystem")
    else()
        if(short STREQUAL "")
            message(FATAL_ERROR
                "the filesystem generates 8.3 short names, but none was "
                "reported for ${WORK_DIR}/a-long-directory-name. The listing "
                "was:\n${listing}")
        endif()
        if(NOT generated)
            message(FATAL_ERROR
                "'${short}' was not recognised as a generated short name")
        endif()
        # The *last component* is the one that matters: every ancestor
        # contributes its own short name to the path.
        get_filename_component(short_name "${short}" NAME)
        if(NOT short_name MATCHES "~")
            message(FATAL_ERROR
                "'${short_name}' is not a generated short name")
        endif()
        # And it must be the same directory, not merely a plausible string.
        if(NOT EXISTS "${short}/marker.txt")
            message(FATAL_ERROR
                "the short path '${short}' does not lead back to the directory "
                "it was taken from")
        endif()
    endif()

elseif(CASE STREQUAL "short-path-of-an-8dot3-name")
    # A name that already fits 8.3 gets no generated short name, so it cannot
    # collide with anything. Its short path is still reported -- the ancestors
    # above it have short names of their own -- but its own last component is
    # unchanged, and that is what the collision search keys on.
    file(MAKE_DIRECTORY "${WORK_DIR}/fits")
    bettercad_has_generated_short_name("${WORK_DIR}/fits" generated)
    if(generated)
        bettercad_short_path("${WORK_DIR}/fits" short)
        message(FATAL_ERROR
            "a name that fits 8.3 was reported as having a generated short "
            "name: ${short}")
    endif()
    bettercad_short_path("${WORK_DIR}/fits" short)
    get_filename_component(short_name "${short}" NAME)
    expect_equal("${short_name}" "fits" "the last component is the name itself")

    bettercad_short_path("${WORK_DIR}/does-not-exist" short)
    expect_equal("${short}" "" "a directory that does not exist")
    bettercad_has_generated_short_name("${WORK_DIR}/does-not-exist" generated)
    if(generated)
        message(FATAL_ERROR "a directory that does not exist has no short name")
    endif()

elseif(CASE STREQUAL "external-root-required")
    expect_guard_failure(
        "set(BETTERCAD_REQUIRE_EXTERNAL_BUILD_ROOT ON)
         bettercad_check_build_root(\"C:/src/bettercad\" \"C:/elsewhere/debug-ext\")"
        "needs.*BETTERCAD_BUILD_ROOT to say where"
        "external-root-required")

elseif(CASE STREQUAL "build-inside-source-tree-rejected")
    # The trap this preset exists to avoid: BETTERCAD_BUILD_ROOT is set, but
    # it points back inside the source tree, so the build lands in the
    # synchronised folder after all.
    set(script "${WORK_DIR}/inside.cmake")
    file(WRITE "${script}"
        "list(APPEND CMAKE_MODULE_PATH \"${MODULE_DIR}\")\n"
        "include(BetterCADBuildLocation)\n"
        "set(BETTERCAD_REQUIRE_EXTERNAL_BUILD_ROOT ON)\n"
        "bettercad_check_build_root(\"C:/src/bettercad\" \"C:/src/bettercad/build/debug-ext\")\n")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "BETTERCAD_BUILD_ROOT=C:/src/bettercad/build"
            "${CMAKE_COMMAND}" -P "${script}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
    )
    if(result EQUAL 0)
        message(FATAL_ERROR "a build directory inside the source tree was accepted:\n${output}")
    endif()
    flatten("${output}" output)
    if(NOT output MATCHES "the build directory is inside it")
        message(FATAL_ERROR "the guard fired but did not explain itself:\n${output}")
    endif()

elseif(CASE STREQUAL "external-root-accepted")
    # And it must accept the case it exists for, or it would block the preset
    # it was written to support.
    set(script "${WORK_DIR}/outside.cmake")
    file(WRITE "${script}"
        "list(APPEND CMAKE_MODULE_PATH \"${MODULE_DIR}\")\n"
        "include(BetterCADBuildLocation)\n"
        "set(BETTERCAD_REQUIRE_EXTERNAL_BUILD_ROOT ON)\n"
        "bettercad_check_build_root(\"C:/src/bettercad\" \"D:/builds/debug-ext\")\n")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "BETTERCAD_BUILD_ROOT=D:/builds"
            "${CMAKE_COMMAND}" -P "${script}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "a build directory outside the source tree was rejected:\n${output}")
    endif()

else()
    message(FATAL_ERROR "BuildLocationChecks.cmake: unknown CASE '${CASE}'")
endif()

message(STATUS "${CASE}: ok")
