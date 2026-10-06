# The zero-match test-filter guard (P16-CLI-001).
#
# THE DEFECT THIS EXISTS TO PREVENT. `ctest -R <pattern>` exits 0 when the
# pattern matches NOTHING. It prints "No tests were found!!!" and returns
# success, so a qualification script that runs a targeted filter and trusts the
# exit code reports PASS having executed no test at all. A renamed test, a
# typo, or a filter written against a different preset's names all produce
# exactly that.
#
# So this asserts the COUNT, not the exit code: the named filters must each
# discover at least a stated minimum, and a filter known to match nothing must
# be seen to match nothing. The second half is what proves the guard itself
# works -- without it, a broken counter would report PASS for every filter.
#
# Inputs:
#   CTEST       the ctest executable
#   BUILD_DIR   the build directory to enumerate in
cmake_minimum_required(VERSION 3.28)

foreach(required CTEST BUILD_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "ZeroMatchGuard.cmake: ${required} is required")
    endif()
endforeach()

# How many tests a filter discovers. `ctest -N` lists without running.
function(discovered pattern out_var)
    execute_process(
        COMMAND "${CTEST}" --test-dir "${BUILD_DIR}" -R "${pattern}" -N
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err
        RESULT_VARIABLE _code)
    if(NOT _code EQUAL 0)
        message(FATAL_ERROR "ctest -N failed for '${pattern}': ${_err}")
    endif()
    # "Total Tests: N" is ctest's own count, so this does not re-implement
    # counting by parsing the listing.
    if(NOT _out MATCHES "Total Tests: ([0-9]+)")
        message(FATAL_ERROR "ctest -N printed no total for '${pattern}':\n${_out}")
    endif()
    set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

# Each filter this milestone relies on, and the minimum it must find. The
# minimums are deliberately below the real counts: the point is to catch a
# filter that matches NOTHING, not to make every added test a maintenance
# task.
set(_filters
    "unit\\.MeshingCli" 10
    "cli\\.mesh\\." 5
    "unit\\.MeshingPersistence" 10
    "unit\\.MeshingCommand" 10
    # P16-REFMOD-001. The reference suite's own two filters: the in-process
    # cases and the fresh-process ones. The qualification runs both, so both
    # have to be seen to match something.
    "unit\\.Mesh(Block|Cylinder|PlateWithHole|Tube|ThinPlate|TransformedBlock|LocalRefinement|OpenProfile|Reference|Curved)" 20
    "refmod\\.mesh\\." 15
)
list(LENGTH _filters _count)
math(EXPR _last "${_count} - 1")
foreach(i RANGE 0 ${_last} 2)
    list(GET _filters ${i} _pattern)
    math(EXPR _j "${i} + 1")
    list(GET _filters ${_j} _minimum)
    discovered("${_pattern}" _found)
    if(_found LESS _minimum)
        message(FATAL_ERROR
            "ZERO-MATCH GUARD FAILED: '${_pattern}' discovered ${_found} tests, "
            "expected at least ${_minimum}. A filter that matches too few tests "
            "makes a targeted run meaningless, and ctest -R exits 0 when it "
            "matches none at all.")
    endif()
    message(STATUS "${_pattern}: ${_found} tests (minimum ${_minimum})")
endforeach()

# AND THE GUARD ITSELF MUST WORK. A pattern that cannot match anything has to
# come back as zero; if this reported a non-zero count, every check above would
# be vacuous.
discovered("unit\\.ThisTestDoesNotExist_P16CLI001" _none)
if(NOT _none EQUAL 0)
    message(FATAL_ERROR
        "the guard is broken: a deliberately impossible filter discovered ${_none} tests")
endif()
message(STATUS "an impossible filter discovers 0 tests, so the counter is real")

# And ctest really does exit 0 on a zero match, which is the whole premise.
execute_process(
    COMMAND "${CTEST}" --test-dir "${BUILD_DIR}" -R "unit\\.ThisTestDoesNotExist_P16CLI001"
    OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _code)
message(STATUS "ctest on an impossible filter exits ${_code} -- which is why counting is required")

message(STATUS "ZeroMatchGuard: every filter discovers tests, and the counter is proven")
